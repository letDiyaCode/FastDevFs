#include "../../include/fuse functions/fs_chown.h"

int fs_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi) {
    (void) fi;
    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    std::string path_str(path);
    int index;
    if (path_str == "/") {
        index = 0;
    } else {
        index = hashindex(path_str, file1);
    }

    if (index == -1) {
        return -ENOENT;
    }

    metadate& meta = file1.arr[index].metadata;
    struct fuse_context* ctx = fuse_get_context();

    // Only superuser can change owner.
    // Owner can change group (to a group they belong to), but we simplify:
    // only superuser or current owner can chown.
    if (ctx->uid != 0 && ctx->uid != meta.uid) {
        return -EPERM;
    }

    // (uid_t)-1 means "don't change"
    if (uid != (uid_t)-1) {
        // Non-root can only chown to themselves
        if (ctx->uid != 0 && uid != ctx->uid) {
            return -EPERM;
        }
        meta.uid = uid;
    }

    if (gid != (gid_t)-1) {
        meta.gid = gid;
    }

    // If non-root changes owner/group, clear setuid/setgid bits (POSIX)
    if (ctx->uid != 0) {
        meta.mode &= ~(S_ISUID | S_ISGID);
    }

    meta.ctime = time(nullptr);

    persist(file1);
    return 0;
}
