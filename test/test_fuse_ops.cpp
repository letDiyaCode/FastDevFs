#include <iostream>
#include <gtest/gtest.h>
#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "../include/daemon/directory tree/adt.h"
#include "../include/fuse functions/fs_getattr.h"
#include "../include/fuse functions/fs_access.h"
#include "../include/fuse functions/fs_mkdir.h"
#include "../include/fuse functions/fs_opendir.h"
#include "../include/fuse functions/fs_readdir.h"
#include "../include/fuse functions/fs_rmdir.h"
#include "../include/fuse functions/fs_statfs.h"
#include "../include/fuse functions/fs_utimens.h"
#include "../include/fuse functions/fs_create.h"
#include "../include/fuse functions/fs_open.h"
#include "../include/fuse functions/fs_read.h"
#include "../include/fuse functions/fs_write.h"
#include "../include/fuse functions/fs_truncate.h"
#include "../include/fuse functions/fs_unlink.h"
#include "../include/fuse functions/fs_chmod.h"
#include "../include/fuse functions/fs_chown.h"


// Mock fuse_get_context — overrides the libfuse symbol at link time
struct fuse_context mock_context;

extern "C" {
    struct fuse_context* fuse_get_context() {
        return &mock_context;
    }
}

class FuseOpsTest : public ::testing::Test {
protected:
    treefile* file;

    void SetUp() override {
        file = new treefile();
        initialize(*file);

        // Setup mock context — use uid/gid 1000 as the "calling user"
        memset(&mock_context, 0, sizeof(struct fuse_context));
        mock_context.private_data = file;
        mock_context.uid = 1000;
        mock_context.gid = 1000;
        mock_context.umask = 022;

        // initialize() already sets up root at index 0 with:
        //   name="/", mode=S_IFDIR|0755, uid=getuid(), gid=getgid()
        //   and hash["/"] = 0
        // Override root uid/gid to match our mock context so the
        // mock user (1000) is the owner of root and can create entries.
        file->arr[0].metadata.uid = 1000;
        file->arr[0].metadata.gid = 1000;
    }

    void TearDown() override {
        delete file;
    }
};

// ============================================================
// Basic operations
// ============================================================

TEST_F(FuseOpsTest, GetAttrRoot) {
    struct stat st;
    int res = fs_getattr("/", &st, NULL);
    EXPECT_EQ(res, 0);
    EXPECT_EQ(st.st_mode & S_IFMT, (mode_t)S_IFDIR);
    EXPECT_EQ(st.st_nlink, 2);
    // Root must report the real stored uid/gid — not 0!
    EXPECT_EQ(st.st_uid, (uid_t)1000);
    EXPECT_EQ(st.st_gid, (gid_t)1000);
}

TEST_F(FuseOpsTest, GetAttrRootReturnsStoredMode) {
    // Change root mode to 0700
    file->arr[0].metadata.mode = S_IFDIR | 0700;
    struct stat st;
    fs_getattr("/", &st, NULL);
    EXPECT_EQ(st.st_mode, (mode_t)(S_IFDIR | 0700));
}

TEST_F(FuseOpsTest, MkDir) {
    int res = fs_mkdir("/testdir", 0755);
    EXPECT_EQ(res, 0);

    struct stat st;
    res = fs_getattr("/testdir", &st, NULL);
    EXPECT_EQ(res, 0);
    EXPECT_EQ(st.st_mode & S_IFMT, (mode_t)S_IFDIR);
    // Created by mock user 1000
    EXPECT_EQ(st.st_uid, (uid_t)1000);
    EXPECT_EQ(st.st_gid, (gid_t)1000);
}

TEST_F(FuseOpsTest, MkDirDuplicate) {
    EXPECT_EQ(fs_mkdir("/dup", 0755), 0);
    EXPECT_EQ(fs_mkdir("/dup", 0755), -EEXIST);
}

TEST_F(FuseOpsTest, CreateAndOpen) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fi.flags = O_CREAT | O_WRONLY;

    int res = fs_create("/testfile", 0644, &fi);
    EXPECT_EQ(res, 0);

    struct stat st;
    res = fs_getattr("/testfile", &st, NULL);
    EXPECT_EQ(res, 0);
    EXPECT_EQ(st.st_mode & S_IFMT, (mode_t)S_IFREG);
    EXPECT_EQ(st.st_uid, (uid_t)1000);
    EXPECT_EQ(st.st_gid, (gid_t)1000);

    // Open for read — owner has 0644 so read is allowed
    fi.flags = O_RDONLY;
    res = fs_open("/testfile", &fi);
    EXPECT_EQ(res, 0);
}

TEST_F(FuseOpsTest, Access) {
    fs_mkdir("/access_dir", 0700);

    // Owner should have rwx
    int res = fs_access("/access_dir", R_OK | W_OK | X_OK);
    EXPECT_EQ(res, 0);

    // Another user should be denied
    mock_context.uid = 1001;
    mock_context.gid = 1001;
    res = fs_access("/access_dir", R_OK);
    EXPECT_EQ(res, -EACCES);

    // Restore
    mock_context.uid = 1000;
    mock_context.gid = 1000;
}

TEST_F(FuseOpsTest, AccessRootUser) {
    fs_mkdir("/secret", 0700);

    // Switch to unprivileged other user
    mock_context.uid = 9999;
    mock_context.gid = 9999;
    EXPECT_EQ(fs_access("/secret", R_OK), -EACCES);

    // Superuser (uid 0) can access everything
    mock_context.uid = 0;
    mock_context.gid = 0;
    EXPECT_EQ(fs_access("/secret", R_OK | W_OK | X_OK), 0);

    mock_context.uid = 1000;
    mock_context.gid = 1000;
}

TEST_F(FuseOpsTest, AccessExistenceOnly) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/exist_check", 0000, &fi);  // No permissions at all

    // F_OK should still succeed — file exists
    mock_context.uid = 9999;
    mock_context.gid = 9999;
    EXPECT_EQ(fs_access("/exist_check", F_OK), 0);

    mock_context.uid = 1000;
    mock_context.gid = 1000;
}

TEST_F(FuseOpsTest, AccessNonExistent) {
    EXPECT_EQ(fs_access("/no_such_path", F_OK), -ENOENT);
}

TEST_F(FuseOpsTest, AccessRoot) {
    // "/" should be accessible by owner
    EXPECT_EQ(fs_access("/", R_OK | X_OK), 0);
}

TEST_F(FuseOpsTest, WriteAndTruncate) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/file", 0644, &fi);

    const char* data = "hello";
    int res = fs_write("/file", data, 5, 0, &fi);
    EXPECT_EQ(res, 5);

    struct stat st;
    fs_getattr("/file", &st, NULL);
    EXPECT_EQ(st.st_size, 5);

    res = fs_truncate("/file", 10, &fi);
    EXPECT_EQ(res, 0);
    fs_getattr("/file", &st, NULL);
    EXPECT_EQ(st.st_size, 10);
}

TEST_F(FuseOpsTest, Unlink) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/todelete", 0644, &fi);

    int res = fs_unlink("/todelete");
    EXPECT_EQ(res, 0);

    struct stat st;
    res = fs_getattr("/todelete", &st, NULL);
    EXPECT_EQ(res, -ENOENT);
}

TEST_F(FuseOpsTest, Rmdir) {
    fs_mkdir("/dir", 0755);

    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/dir/file", 0644, &fi);

    int res = fs_rmdir("/dir");
    EXPECT_EQ(res, -ENOTEMPTY);

    fs_unlink("/dir/file");

    res = fs_rmdir("/dir");
    EXPECT_EQ(res, 0);
}

TEST_F(FuseOpsTest, ReadDir) {
    fs_mkdir("/lsdir", 0755);
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/lsdir/a", 0644, &fi);
    fs_create("/lsdir/b", 0644, &fi);

    auto filler_mock = [](void *buf, const char *name, const struct stat *stbuf,
                          off_t off, enum fuse_fill_dir_flags flags) -> int {
        std::vector<std::string>* names = static_cast<std::vector<std::string>*>(buf);
        names->push_back(name);
        return 0;
    };

    std::vector<std::string> names;
    int res = fs_readdir("/lsdir", &names, filler_mock, 0, &fi, (fuse_readdir_flags)0);
    EXPECT_EQ(res, 0);

    bool found_dot = false, found_dotdot = false, found_a = false, found_b = false;
    for (const auto& name : names) {
        if (name == ".") found_dot = true;
        if (name == "..") found_dotdot = true;
        if (name == "a") found_a = true;
        if (name == "b") found_b = true;
    }
    EXPECT_TRUE(found_dot);
    EXPECT_TRUE(found_dotdot);
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);
}

TEST_F(FuseOpsTest, ReadTest) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/readfile", 0644, &fi);

    const char* data = "test data";
    fs_write("/readfile", data, 9, 0, &fi);

    char buf[32] = {};
    int res = fs_read("/readfile", buf, 9, 0, &fi);
    EXPECT_EQ(res, 9);
    EXPECT_EQ(memcmp(buf, "test data", 9), 0);
}

TEST_F(FuseOpsTest, UtimensTest) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/timefile", 0644, &fi);

    struct timespec ts[2];
    ts[0].tv_sec = 1000;
    ts[0].tv_nsec = 0;
    ts[1].tv_sec = 2000;
    ts[1].tv_nsec = 0;

    int res = fs_utimens("/timefile", ts, &fi);
    EXPECT_EQ(res, 0);

    struct stat st;
    fs_getattr("/timefile", &st, NULL);
    EXPECT_EQ(st.st_atime, 1000);
    EXPECT_EQ(st.st_mtime, 2000);
}

TEST_F(FuseOpsTest, StatfsTest) {
    struct statvfs st;
    int res = fs_statfs("/", &st);
    EXPECT_EQ(res, 0);
    EXPECT_GT(st.f_blocks, (fsblkcnt_t)0);
    EXPECT_GT(st.f_bfree, (fsblkcnt_t)0);
    EXPECT_GT(st.f_bavail, (fsblkcnt_t)0);
}

TEST_F(FuseOpsTest, OpenDir) {
    fs_mkdir("/opendir_test", 0755);
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    int res = fs_opendir("/opendir_test", &fi);
    EXPECT_EQ(res, 0);

    struct fuse_file_info fi2;
    memset(&fi2, 0, sizeof(fi2));
    fs_create("/opendir_file", 0644, &fi2);
    res = fs_opendir("/opendir_file", &fi2);
    EXPECT_EQ(res, -ENOTDIR);

    res = fs_opendir("/opendir_ghost", &fi);
    EXPECT_EQ(res, -ENOENT);
}

TEST_F(FuseOpsTest, CreateExcl) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fi.flags = O_CREAT | O_EXCL | O_WRONLY;

    int res = fs_create("/excl_file", 0644, &fi);
    EXPECT_EQ(res, 0);

    // Second create should return EEXIST
    res = fs_create("/excl_file", 0644, &fi);
    EXPECT_EQ(res, -EEXIST);
}

TEST_F(FuseOpsTest, GetAttrNested) {
    fs_mkdir("/l1", 0755);
    fs_mkdir("/l1/l2", 0755);
    fs_mkdir("/l1/l2/l3", 0755);
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/l1/l2/l3/file", 0644, &fi);

    struct stat st;
    EXPECT_EQ(fs_getattr("/l1/l2/l3/file", &st, NULL), 0);
    EXPECT_EQ(st.st_mode & S_IFMT, (mode_t)S_IFREG);
}

TEST_F(FuseOpsTest, ReadOffset) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/read_off", 0644, &fi);
    char buf[10];
    int res = fs_read("/read_off", buf, 5, 100, &fi);
    EXPECT_EQ(res, 0);
}

TEST_F(FuseOpsTest, WriteAppend) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/append", 0644, &fi);
    const char* data = "123";
    fs_write("/append", data, 3, 0, &fi);

    struct stat st;
    fs_getattr("/append", &st, NULL);
    EXPECT_EQ(st.st_size, 3);

    fs_write("/append", data, 3, 3, &fi);
    fs_getattr("/append", &st, NULL);
    EXPECT_EQ(st.st_size, 6);
}

TEST_F(FuseOpsTest, TruncateExpand) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/expand", 0644, &fi);
    fs_truncate("/expand", 100, &fi);

    struct stat st;
    fs_getattr("/expand", &st, NULL);
    EXPECT_EQ(st.st_size, 100);
}

TEST_F(FuseOpsTest, AccessWriteProtect) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/readonly", 0444, &fi);

    // Owner: mode 0444 means no write for anyone
    EXPECT_EQ(fs_access("/readonly", W_OK), -EACCES);
    EXPECT_EQ(fs_access("/readonly", R_OK), 0);

    // F_OK should pass
    EXPECT_EQ(fs_access("/readonly", F_OK), 0);
}

TEST_F(FuseOpsTest, UnlinkMissing) {
    EXPECT_EQ(fs_unlink("/missing_file_unlink"), -ENOENT);
}

TEST_F(FuseOpsTest, UtimensNull) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/now", 0644, &fi);
    EXPECT_EQ(fs_utimens("/now", NULL, &fi), 0);
}

TEST_F(FuseOpsTest, StatfsCheck) {
    struct statvfs st;
    EXPECT_EQ(fs_statfs("/anything", &st), 0);
    EXPECT_EQ(st.f_namemax, (unsigned long)255);
}

// ============================================================
// chmod / chown tests
// ============================================================

TEST_F(FuseOpsTest, ChmodBasic) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/chfile", 0644, &fi);

    // Owner can chmod
    int res = fs_chmod("/chfile", 0755, NULL);
    EXPECT_EQ(res, 0);

    struct stat st;
    fs_getattr("/chfile", &st, NULL);
    EXPECT_EQ(st.st_mode, (mode_t)(S_IFREG | 0755));
}

TEST_F(FuseOpsTest, ChmodPreservesType) {
    fs_mkdir("/chdir", 0755);

    fs_chmod("/chdir", 0700, NULL);

    struct stat st;
    fs_getattr("/chdir", &st, NULL);
    // Must still be a directory
    EXPECT_EQ(st.st_mode & S_IFMT, (mode_t)S_IFDIR);
    EXPECT_EQ(st.st_mode & 07777, (mode_t)0700);
}

TEST_F(FuseOpsTest, ChmodDeniedNonOwner) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/owned", 0644, &fi);

    // Switch to another user
    mock_context.uid = 2000;
    mock_context.gid = 2000;
    EXPECT_EQ(fs_chmod("/owned", 0777, NULL), -EPERM);

    mock_context.uid = 1000;
    mock_context.gid = 1000;
}

TEST_F(FuseOpsTest, ChmodAllowedByRoot) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/rootchmod", 0600, &fi);

    // Superuser can chmod anything
    mock_context.uid = 0;
    mock_context.gid = 0;
    EXPECT_EQ(fs_chmod("/rootchmod", 0777, NULL), 0);

    struct stat st;
    fs_getattr("/rootchmod", &st, NULL);
    EXPECT_EQ(st.st_mode & 07777, (mode_t)0777);

    mock_context.uid = 1000;
    mock_context.gid = 1000;
}

TEST_F(FuseOpsTest, ChmodNonExistent) {
    EXPECT_EQ(fs_chmod("/no_such_file", 0644, NULL), -ENOENT);
}

TEST_F(FuseOpsTest, ChmodRoot) {
    // Owner can chmod root directory
    EXPECT_EQ(fs_chmod("/", 0700, NULL), 0);

    struct stat st;
    fs_getattr("/", &st, NULL);
    EXPECT_EQ(st.st_mode, (mode_t)(S_IFDIR | 0700));
}

TEST_F(FuseOpsTest, ChownBasic) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/ownfile", 0644, &fi);

    // As superuser, change owner
    mock_context.uid = 0;
    mock_context.gid = 0;
    int res = fs_chown("/ownfile", 2000, 2000, NULL);
    EXPECT_EQ(res, 0);

    struct stat st;
    fs_getattr("/ownfile", &st, NULL);
    EXPECT_EQ(st.st_uid, (uid_t)2000);
    EXPECT_EQ(st.st_gid, (gid_t)2000);

    mock_context.uid = 1000;
    mock_context.gid = 1000;
}

TEST_F(FuseOpsTest, ChownDeniedNonOwner) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/ownfile2", 0644, &fi);

    // Another non-root, non-owner user
    mock_context.uid = 3000;
    mock_context.gid = 3000;
    EXPECT_EQ(fs_chown("/ownfile2", 3000, 3000, NULL), -EPERM);

    mock_context.uid = 1000;
    mock_context.gid = 1000;
}

TEST_F(FuseOpsTest, ChownChangeGroupOnly) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/grpfile", 0644, &fi);

    // Owner changes group only (uid = -1 means "don't change")
    int res = fs_chown("/grpfile", (uid_t)-1, 5000, NULL);
    EXPECT_EQ(res, 0);

    struct stat st;
    fs_getattr("/grpfile", &st, NULL);
    EXPECT_EQ(st.st_uid, (uid_t)1000);  // unchanged
    EXPECT_EQ(st.st_gid, (gid_t)5000);  // changed
}

TEST_F(FuseOpsTest, ChownNonExistent) {
    EXPECT_EQ(fs_chown("/no_such", 1000, 1000, NULL), -ENOENT);
}

// ============================================================
// Permission enforcement across operations
// ============================================================

TEST_F(FuseOpsTest, MkDirDeniedNoWriteOnParent) {
    // Create a directory owned by 1000 but mode 0555 (r-xr-xr-x — no write)
    fs_mkdir("/nowrite", 0555);

    // Owner can't create inside because no write bit
    EXPECT_EQ(fs_mkdir("/nowrite/sub", 0755), -EACCES);
}

TEST_F(FuseOpsTest, CreateDeniedNoWriteOnParent) {
    fs_mkdir("/nowrite2", 0555);

    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    EXPECT_EQ(fs_create("/nowrite2/file", 0644, &fi), -EACCES);
}

TEST_F(FuseOpsTest, OpenDeniedNoReadPerm) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/secret_file", 0200, &fi);  // write-only for owner

    fi.flags = O_RDONLY;
    EXPECT_EQ(fs_open("/secret_file", &fi), -EACCES);

    fi.flags = O_WRONLY;
    EXPECT_EQ(fs_open("/secret_file", &fi), 0);  // write is allowed
}

TEST_F(FuseOpsTest, TruncateDeniedNoWritePerm) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/trunc_test", 0444, &fi);  // read-only

    EXPECT_EQ(fs_truncate("/trunc_test", 0, NULL), -EACCES);
}

TEST_F(FuseOpsTest, UnlinkDeniedNoWriteOnParent) {
    // Root dir with write+exec: owner can create files
    fs_mkdir("/protdir", 0755);

    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/protdir/victim", 0644, &fi);

    // Remove write from parent
    fs_chmod("/protdir", 0555, NULL);

    // Now unlink should fail — parent has no write permission
    EXPECT_EQ(fs_unlink("/protdir/victim"), -EACCES);

    // Restore so TearDown can clean up
    fs_chmod("/protdir", 0755, NULL);
}

TEST_F(FuseOpsTest, RmdirDeniedNoWriteOnParent) {
    fs_mkdir("/protdir2", 0755);
    fs_mkdir("/protdir2/sub", 0755);

    // Remove write from parent
    fs_chmod("/protdir2", 0555, NULL);

    EXPECT_EQ(fs_rmdir("/protdir2/sub"), -EACCES);

    // Restore
    fs_chmod("/protdir2", 0755, NULL);
}

TEST_F(FuseOpsTest, UtimensDeniedNonOwner) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/timeguard", 0644, &fi);

    struct timespec ts[2] = {{5000, 0}, {6000, 0}};

    // Another user (not owner, not root) — no write perm in "others" either (0644 = rw-r--r--)
    mock_context.uid = 9999;
    mock_context.gid = 9999;
    EXPECT_EQ(fs_utimens("/timeguard", ts, &fi), -EPERM);

    mock_context.uid = 1000;
    mock_context.gid = 1000;
}

TEST_F(FuseOpsTest, UtimensOnRoot) {
    struct timespec ts[2] = {{100, 0}, {200, 0}};
    EXPECT_EQ(fs_utimens("/", ts, NULL), 0);

    struct stat st;
    fs_getattr("/", &st, NULL);
    EXPECT_EQ(st.st_atime, 100);
    EXPECT_EQ(st.st_mtime, 200);
}

TEST_F(FuseOpsTest, OpendirDeniedNoReadPerm) {
    fs_mkdir("/privdir", 0300);  // wx for owner, no read

    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    EXPECT_EQ(fs_opendir("/privdir", &fi), -EACCES);
}

TEST_F(FuseOpsTest, GroupPermissions) {
    // Create file owned by 1000:1000 with mode 0070 (rwx for group only)
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/grptest", 0070, &fi);

    // Owner (1000) has NO owner bits — should fail read
    EXPECT_EQ(fs_access("/grptest", R_OK), -EACCES);

    // User in same group (different uid, same gid 1000)
    mock_context.uid = 2000;
    mock_context.gid = 1000;
    EXPECT_EQ(fs_access("/grptest", R_OK | W_OK | X_OK), 0);

    mock_context.uid = 1000;
    mock_context.gid = 1000;
}

TEST_F(FuseOpsTest, OtherPermissions) {
    // Create file owned by 1000:1000 with mode 0007 (rwx for others only)
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/othtest", 0007, &fi);

    // Owner should fail
    EXPECT_EQ(fs_access("/othtest", R_OK), -EACCES);

    // Random user (different uid AND gid)
    mock_context.uid = 5000;
    mock_context.gid = 5000;
    EXPECT_EQ(fs_access("/othtest", R_OK | W_OK | X_OK), 0);

    mock_context.uid = 1000;
    mock_context.gid = 1000;
}

// ============================================================
// Data operations — write / read / truncate round-trips
// ============================================================

TEST_F(FuseOpsTest, WriteReadRoundTrip) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/rt", 0644, &fi);

    const char* msg = "Hello, FastDevFs!";
    size_t len = strlen(msg);
    EXPECT_EQ(fs_write("/rt", msg, len, 0, &fi), (int)len);

    char buf[64] = {};
    EXPECT_EQ(fs_read("/rt", buf, sizeof(buf), 0, &fi), (int)len);
    EXPECT_EQ(std::string(buf, len), "Hello, FastDevFs!");
}

TEST_F(FuseOpsTest, ReadPastEOF) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/eof", 0644, &fi);

    fs_write("/eof", "abc", 3, 0, &fi);

    char buf[16] = {};
    // Offset beyond file size → EOF
    EXPECT_EQ(fs_read("/eof", buf, 10, 100, &fi), 0);

    // Read from offset 1, only 2 bytes remain (bc)
    EXPECT_EQ(fs_read("/eof", buf, 10, 1, &fi), 2);
    EXPECT_EQ(buf[0], 'b');
    EXPECT_EQ(buf[1], 'c');
}

TEST_F(FuseOpsTest, WriteAtOffset) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/woff", 0644, &fi);

    fs_write("/woff", "AAAA", 4, 0, &fi);
    fs_write("/woff", "BB", 2, 2, &fi);   // overwrite bytes 2..3

    char buf[8] = {};
    fs_read("/woff", buf, 4, 0, &fi);
    EXPECT_EQ(buf[0], 'A');
    EXPECT_EQ(buf[1], 'A');
    EXPECT_EQ(buf[2], 'B');
    EXPECT_EQ(buf[3], 'B');
}

TEST_F(FuseOpsTest, WriteBeyondCurrentSize) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/gap", 0644, &fi);

    // Write at offset 10 — bytes 0..9 should be NUL (from zero-init)
    fs_write("/gap", "XY", 2, 10, &fi);

    struct stat st;
    fs_getattr("/gap", &st, NULL);
    EXPECT_EQ(st.st_size, 12);

    char buf[16] = {};
    fs_read("/gap", buf, 16, 0, &fi);
    // The gap should be NUL
    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(buf[i], '\0') << "byte " << i << " should be NUL";
    }
    EXPECT_EQ(buf[10], 'X');
    EXPECT_EQ(buf[11], 'Y');
}

TEST_F(FuseOpsTest, TruncateClearsData) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/trdata", 0644, &fi);

    fs_write("/trdata", "abcdef", 6, 0, &fi);
    // Truncate to 3 → bytes 3..5 must be cleared
    fs_truncate("/trdata", 3, &fi);

    struct stat st;
    fs_getattr("/trdata", &st, NULL);
    EXPECT_EQ(st.st_size, 3);

    // Re-grow to 6 — new bytes should be NUL
    fs_truncate("/trdata", 6, &fi);
    char buf[8] = {};
    fs_read("/trdata", buf, 6, 0, &fi);
    EXPECT_EQ(buf[0], 'a');
    EXPECT_EQ(buf[1], 'b');
    EXPECT_EQ(buf[2], 'c');
    EXPECT_EQ(buf[3], '\0');
    EXPECT_EQ(buf[4], '\0');
    EXPECT_EQ(buf[5], '\0');
}

TEST_F(FuseOpsTest, OpenWithTrunc) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/otr", 0644, &fi);

    fs_write("/otr", "hello world", 11, 0, &fi);

    // Open with O_TRUNC
    fi.flags = O_WRONLY | O_TRUNC;
    EXPECT_EQ(fs_open("/otr", &fi), 0);

    struct stat st;
    fs_getattr("/otr", &st, NULL);
    EXPECT_EQ(st.st_size, 0);

    // Data should be gone
    char buf[16] = {};
    fi.flags = O_RDONLY;
    fs_open("/otr", &fi);
    EXPECT_EQ(fs_read("/otr", buf, 16, 0, &fi), 0);  // EOF
}

TEST_F(FuseOpsTest, CreateReOpenExisting) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fi.flags = O_CREAT | O_WRONLY;
    EXPECT_EQ(fs_create("/reopen", 0644, &fi), 0);
    fs_write("/reopen", "first", 5, 0, &fi);

    // Create again without O_EXCL — should NOT fail
    fi.flags = O_CREAT | O_WRONLY | O_TRUNC;
    EXPECT_EQ(fs_create("/reopen", 0644, &fi), 0);

    // File should be truncated (size 0) because O_TRUNC
    struct stat st;
    fs_getattr("/reopen", &st, NULL);
    EXPECT_EQ(st.st_size, 0);
}

TEST_F(FuseOpsTest, CreateReOpenPreservesData) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fi.flags = O_CREAT | O_WRONLY;
    fs_create("/reopkeep", 0644, &fi);
    fs_write("/reopkeep", "keep", 4, 0, &fi);

    // Create again without O_TRUNC — data should survive
    fi.flags = O_CREAT | O_WRONLY;
    EXPECT_EQ(fs_create("/reopkeep", 0644, &fi), 0);

    char buf[8] = {};
    EXPECT_EQ(fs_read("/reopkeep", buf, 8, 0, &fi), 4);
    EXPECT_EQ(memcmp(buf, "keep", 4), 0);
}

TEST_F(FuseOpsTest, LargeWriteClamped) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/big", 0644, &fi);

    // Attempt to write more than MAX_FILE_DATA bytes
    char big[MAX_FILE_DATA + 100];
    memset(big, 'X', sizeof(big));
    int wrote = fs_write("/big", big, sizeof(big), 0, &fi);
    // Should clamp to MAX_FILE_DATA
    EXPECT_EQ(wrote, MAX_FILE_DATA);

    struct stat st;
    fs_getattr("/big", &st, NULL);
    EXPECT_EQ(st.st_size, MAX_FILE_DATA);
}

TEST_F(FuseOpsTest, WritePastBufferReturnsEFBIG) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/toobig", 0644, &fi);

    // Write at offset == MAX_FILE_DATA → no room at all
    EXPECT_EQ(fs_write("/toobig", "x", 1, MAX_FILE_DATA, &fi), -EFBIG);
}

TEST_F(FuseOpsTest, TruncateBeyondMaxReturnsEFBIG) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/truncbig", 0644, &fi);

    EXPECT_EQ(fs_truncate("/truncbig", MAX_FILE_DATA + 1, &fi), -EFBIG);
}

TEST_F(FuseOpsTest, ReadOnDir) {
    fs_mkdir("/rdir", 0755);
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    char buf[16];
    EXPECT_EQ(fs_read("/rdir", buf, 16, 0, &fi), -EISDIR);
}

TEST_F(FuseOpsTest, WriteOnDir) {
    fs_mkdir("/wdir", 0755);
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    EXPECT_EQ(fs_write("/wdir", "x", 1, 0, &fi), -EISDIR);
}

TEST_F(FuseOpsTest, UnlinkClearsData) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/deldata", 0644, &fi);
    fs_write("/deldata", "secret", 6, 0, &fi);
    fs_unlink("/deldata");

    // Recreate same path — data should be zero, not stale
    fs_create("/deldata", 0644, &fi);
    struct stat st;
    fs_getattr("/deldata", &st, NULL);
    EXPECT_EQ(st.st_size, 0);

    char buf[8] = {};
    EXPECT_EQ(fs_read("/deldata", buf, 8, 0, &fi), 0);  // EOF, size is 0
}

TEST_F(FuseOpsTest, MultipleFilesIndependent) {
    struct fuse_file_info fi;
    memset(&fi, 0, sizeof(fi));
    fs_create("/f1", 0644, &fi);
    fs_create("/f2", 0644, &fi);

    fs_write("/f1", "alpha", 5, 0, &fi);
    fs_write("/f2", "beta", 4, 0, &fi);

    char buf1[8] = {}, buf2[8] = {};
    EXPECT_EQ(fs_read("/f1", buf1, 8, 0, &fi), 5);
    EXPECT_EQ(fs_read("/f2", buf2, 8, 0, &fi), 4);
    EXPECT_EQ(memcmp(buf1, "alpha", 5), 0);
    EXPECT_EQ(memcmp(buf2, "beta", 4), 0);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
