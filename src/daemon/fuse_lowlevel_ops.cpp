#define FUSE_USE_VERSION 31

#include "../../include/daemon/fuse_lowlevel_ops.h"
#include "../../include/daemon/directory tree/adt.h"
#include "../../include/daemon/directory tree/hash.h"

#include <fuse3/fuse_lowlevel.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "../../include/dedup_ipc.h"

using namespace std;

// ============================================================
// Helpers
// ============================================================

static void send_dedup_request(uint64_t inode, const string& path, int op_type) {
    int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0) return;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DEDUP_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    DedupRequest req;
    req.inode = inode;
    strncpy(req.path, path.c_str(), sizeof(req.path) - 1);
    req.operation_type = op_type;

    sendto(sock, &req, sizeof(req), 0, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
}

// Get treefile* from the FUSE request's userdata.
static treefile* get_treefile(fuse_req_t req) {
    return (treefile*)fuse_req_userdata(req);
}

// Build the full path of a node — now just returns metadata.name directly
// since we store full absolute paths in each node.
static string build_full_path(int index, treefile& tf) {
    if (index < 0 || index >= tf.size) return "";
    return tf.arr[index].metadata.name;
}

// Extract the basename from a full path (e.g. "/dir/file.txt" -> "file.txt")
static string basename_of(const char* path) {
    const char* last_slash = strrchr(path, '/');
    if (last_slash && *(last_slash + 1) != '\0') {
        return string(last_slash + 1);
    }
    return string(path);
}

// Build host-FS data file path for a tree array index.
static string host_data_path(int index) {
    return string(FASTDEVFS_DATA_DIR) + "/" + to_string(index);
}

// Fill a struct stat from a treenode's metadata.
static void fill_stat(struct stat* st, const treenode& node, int index) {
    memset(st, 0, sizeof(*st));
    st->st_ino = index_to_ino(index);
    st->st_mode = node.metadata.mode;
    st->st_nlink = node.metadata.nlink;
    st->st_uid = node.metadata.uid;
    st->st_gid = node.metadata.gid;
    st->st_size = node.metadata.size;
    st->st_atime = node.metadata.atime;
    st->st_mtime = node.metadata.mtime;
    st->st_ctime = node.metadata.ctime;
    st->st_blksize = 4096;
    st->st_blocks = (st->st_size + 511) / 512;
}

// Fill a fuse_entry_param from a treenode.
static void fill_entry(struct fuse_entry_param* e, const treenode& node, int index) {
    memset(e, 0, sizeof(*e));
    e->ino = index_to_ino(index);
    e->generation = 1;
    e->attr_timeout = 1.0;
    e->entry_timeout = 1.0;
    fill_stat(&e->attr, node, index);
}

// Find a child of parent_index by basename.
// Compares the basename portion of each child's full-path metadata.name.
// Returns the child's array index, or -1 if not found.
static int find_child_by_name(int parent_index, const char* name, treefile& tf) {
    if (parent_index < 0 || parent_index >= tf.size) return -1;
    if (tf.arr[parent_index].isdeleted) return -1;

    int child = tf.arr[parent_index].firstchild;
    int safety = tf.size;
    while (child >= 0 && child < tf.size && safety-- > 0) {
        if (!tf.arr[child].isdeleted) {
            string child_basename = basename_of(tf.arr[child].metadata.name);
            if (child_basename == name) {
                return child;
            }
        }
        child = tf.arr[child].nextsibling;
    }
    return -1;
}

// Check if a directory node has any children (non-deleted).
static bool dir_is_empty(int index, treefile& tf) {
    int child = tf.arr[index].firstchild;
    int safety = tf.size;
    while (child >= 0 && child < tf.size && safety-- > 0) {
        if (!tf.arr[child].isdeleted) return false;
        child = tf.arr[child].nextsibling;
    }
    return true;
}

// ============================================================
// Low-level FUSE callbacks
// ============================================================

static void ll_init(void* userdata, struct fuse_conn_info* conn) {
    (void)conn;
    (void)userdata;
    // Ensure the host-FS data directory exists.
    mkdir(FASTDEVFS_DATA_DIR, 0755);
}

static void ll_destroy(void* userdata) {
    (void)userdata;
    // mmap sync/close is handled in main.cpp
}

static void ll_lookup(fuse_req_t req, fuse_ino_t parent, const char* name) {
    treefile* tf = get_treefile(req);
    int parent_idx = ino_to_index(parent);

    if (parent_idx < 0 || parent_idx >= tf->size ||
        tf->arr[parent_idx].isdeleted) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    // Check it's a directory
    if (!S_ISDIR(tf->arr[parent_idx].metadata.mode)) {
        fuse_reply_err(req, ENOTDIR);
        return;
    }

    int child_idx = find_child_by_name(parent_idx, name, *tf);
    if (child_idx < 0) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    struct fuse_entry_param e;
    fill_entry(&e, tf->arr[child_idx], child_idx);
    fuse_reply_entry(req, &e);
}

static void ll_forget(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup) {
    (void)ino;
    (void)nlookup;
    fuse_reply_none(req);
}

static void ll_getattr(fuse_req_t req, fuse_ino_t ino,
                       struct fuse_file_info* fi) {
    (void)fi;
    treefile* tf = get_treefile(req);
    int idx = ino_to_index(ino);

    if (idx < 0 || idx >= tf->size || tf->arr[idx].isdeleted) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    // For regular files, sync size from host-FS file if it exists
    if (S_ISREG(tf->arr[idx].metadata.mode)) {
        string path = host_data_path(idx);
        struct stat host_st;
        if (stat(path.c_str(), &host_st) == 0) {
            tf->arr[idx].metadata.size = host_st.st_size;
        }
    }

    struct stat st;
    fill_stat(&st, tf->arr[idx], idx);
    fuse_reply_attr(req, &st, 1.0);
}

static void ll_setattr(fuse_req_t req, fuse_ino_t ino, struct stat* attr,
                       int to_set, struct fuse_file_info* fi) {
    (void)fi;
    treefile* tf = get_treefile(req);
    int idx = ino_to_index(ino);

    if (idx < 0 || idx >= tf->size || tf->arr[idx].isdeleted) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    treenode& node = tf->arr[idx];

    if (to_set & FUSE_SET_ATTR_MODE) {
        // Preserve file type bits, only update permission bits
        node.metadata.mode = (node.metadata.mode & S_IFMT) | (attr->st_mode & 07777);
    }
    if (to_set & FUSE_SET_ATTR_UID) {
        node.metadata.uid = attr->st_uid;
    }
    if (to_set & FUSE_SET_ATTR_GID) {
        node.metadata.gid = attr->st_gid;
    }
    if (to_set & FUSE_SET_ATTR_SIZE) {
        // Truncate the host-FS file
        if (S_ISREG(node.metadata.mode)) {
            string path = host_data_path(idx);
            if (truncate(path.c_str(), attr->st_size) == -1) {
                // File may not exist yet; create it
                int fd = open(path.c_str(), O_CREAT | O_WRONLY, 0644);
                if (fd >= 0) {
                    ftruncate(fd, attr->st_size);
                    close(fd);
                }
            }
        }
        node.metadata.size = attr->st_size;
    }
    if (to_set & FUSE_SET_ATTR_ATIME) {
        node.metadata.atime = attr->st_atime;
    }
    if (to_set & FUSE_SET_ATTR_MTIME) {
        node.metadata.mtime = attr->st_mtime;
    }
    if (to_set & FUSE_SET_ATTR_ATIME_NOW) {
        node.metadata.atime = time(nullptr);
    }
    if (to_set & FUSE_SET_ATTR_MTIME_NOW) {
        node.metadata.mtime = time(nullptr);
    }

    node.metadata.ctime = time(nullptr);

    struct stat st;
    fill_stat(&st, node, idx);
    fuse_reply_attr(req, &st, 1.0);
}

static void ll_mkdir(fuse_req_t req, fuse_ino_t parent, const char* name,
                     mode_t mode) {
    treefile* tf = get_treefile(req);
    int parent_idx = ino_to_index(parent);

    if (parent_idx < 0 || parent_idx >= tf->size ||
        tf->arr[parent_idx].isdeleted) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    // Check if name already exists under parent
    if (find_child_by_name(parent_idx, name, *tf) >= 0) {
        fuse_reply_err(req, EEXIST);
        return;
    }

    // Build full path for the new directory
    string parent_path = build_full_path(parent_idx, *tf);
    string child_path;
    if (parent_path == "/") {
        child_path = "/" + string(name);
    } else {
        child_path = parent_path + "/" + string(name);
    }

    // Insert into the directory tree using full paths
    insertfolder(child_path, parent_path, *tf);

    // Find the newly inserted node
    int child_idx = find_child_by_name(parent_idx, name, *tf);
    if (child_idx < 0) {
        fuse_reply_err(req, ENOSPC);
        return;
    }

    // Apply requested mode and caller's uid/gid
    const struct fuse_ctx* ctx = fuse_req_ctx(req);
    tf->arr[child_idx].metadata.mode = S_IFDIR | (mode & 07777);
    tf->arr[child_idx].metadata.uid = ctx->uid;
    tf->arr[child_idx].metadata.gid = ctx->gid;

    struct fuse_entry_param e;
    fill_entry(&e, tf->arr[child_idx], child_idx);
    fuse_reply_entry(req, &e);
}

static void ll_unlink(fuse_req_t req, fuse_ino_t parent, const char* name) {
    treefile* tf = get_treefile(req);
    int parent_idx = ino_to_index(parent);

    if (parent_idx < 0 || parent_idx >= tf->size ||
        tf->arr[parent_idx].isdeleted) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    int child_idx = find_child_by_name(parent_idx, name, *tf);
    if (child_idx < 0) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    if (S_ISDIR(tf->arr[child_idx].metadata.mode)) {
        fuse_reply_err(req, EISDIR);
        return;
    }

    // Remove host-FS data file
    string data_path = host_data_path(child_idx);
    unlink(data_path.c_str()); // OK if it doesn't exist

    // Build full path and delete by full path
    string child_path = build_full_path(child_idx, *tf);
    delete1(child_path, *tf);

    fuse_reply_err(req, 0);
}

static void ll_rmdir(fuse_req_t req, fuse_ino_t parent, const char* name) {
    treefile* tf = get_treefile(req);
    int parent_idx = ino_to_index(parent);

    if (parent_idx < 0 || parent_idx >= tf->size ||
        tf->arr[parent_idx].isdeleted) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    int child_idx = find_child_by_name(parent_idx, name, *tf);
    if (child_idx < 0) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    if (!S_ISDIR(tf->arr[child_idx].metadata.mode)) {
        fuse_reply_err(req, ENOTDIR);
        return;
    }

    if (!dir_is_empty(child_idx, *tf)) {
        fuse_reply_err(req, ENOTEMPTY);
        return;
    }

    // Build full path and delete by full path
    string child_path = build_full_path(child_idx, *tf);
    delete1(child_path, *tf);

    // Decrement parent nlink
    if (tf->arr[parent_idx].metadata.nlink > 2) {
        tf->arr[parent_idx].metadata.nlink--;
    }

    fuse_reply_err(req, 0);
}

static void ll_create(fuse_req_t req, fuse_ino_t parent, const char* name,
                      mode_t mode, struct fuse_file_info* fi) {
    treefile* tf = get_treefile(req);
    int parent_idx = ino_to_index(parent);

    if (parent_idx < 0 || parent_idx >= tf->size ||
        tf->arr[parent_idx].isdeleted) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    // Check if file already exists
    int existing = find_child_by_name(parent_idx, name, *tf);
    if (existing >= 0) {
        // File exists — open it (like O_CREAT without O_EXCL)
        string data_path = host_data_path(existing);
        int flags = O_RDWR | O_CREAT;
        if (fi->flags & O_TRUNC) {
            flags |= O_TRUNC;
        }
        int fd = open(data_path.c_str(), flags, 0644);
        if (fd < 0) {
            fuse_reply_err(req, errno);
            return;
        }
        fi->fh = (uint64_t)fd;

        if (fi->flags & O_TRUNC) {
            tf->arr[existing].metadata.size = 0;
        }
        tf->arr[existing].metadata.atime = time(nullptr);

        struct fuse_entry_param e;
        fill_entry(&e, tf->arr[existing], existing);
        fuse_reply_create(req, &e, fi);
        return;
    }

    // Build full path for the new file
    string parent_path = build_full_path(parent_idx, *tf);
    string child_path;
    if (parent_path == "/") {
        child_path = "/" + string(name);
    } else {
        child_path = parent_path + "/" + string(name);
    }

    // Insert file into directory tree using full paths
    insertfile(child_path, parent_path, *tf);

    // Find the newly created node
    int child_idx = find_child_by_name(parent_idx, name, *tf);
    if (child_idx < 0) {
        fuse_reply_err(req, ENOSPC);
        return;
    }

    // Set proper mode and ownership
    const struct fuse_ctx* ctx = fuse_req_ctx(req);
    tf->arr[child_idx].metadata.mode = S_IFREG | (mode & 07777);
    tf->arr[child_idx].metadata.uid = ctx->uid;
    tf->arr[child_idx].metadata.gid = ctx->gid;

    // Create host-FS data file
    string data_path = host_data_path(child_idx);
    int fd = open(data_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fuse_reply_err(req, errno);
        return;
    }

    // Store host inode
    struct stat host_st;
    if (fstat(fd, &host_st) == 0) {
        tf->arr[child_idx].metadata.inode = (int)host_st.st_ino;
    }

    fi->fh = (uint64_t)fd;

    struct fuse_entry_param e;
    fill_entry(&e, tf->arr[child_idx], child_idx);
    
    // Notify dedup thread of file insertion
    send_dedup_request(e.ino, child_path, 1);

    fuse_reply_create(req, &e, fi);
}

static void ll_open(fuse_req_t req, fuse_ino_t ino,
                    struct fuse_file_info* fi) {
    treefile* tf = get_treefile(req);
    int idx = ino_to_index(ino);

    if (idx < 0 || idx >= tf->size || tf->arr[idx].isdeleted) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    if (S_ISDIR(tf->arr[idx].metadata.mode)) {
        fuse_reply_err(req, EISDIR);
        return;
    }

    // Open the host-FS data file
    string data_path = host_data_path(idx);
    int flags = O_RDWR;

    // Create if it doesn't exist (handles files from before host-FS storage)
    int fd = open(data_path.c_str(), flags | O_CREAT, 0644);
    if (fd < 0) {
        fuse_reply_err(req, errno);
        return;
    }

    // Handle O_TRUNC
    if (fi->flags & O_TRUNC) {
        ftruncate(fd, 0);
        tf->arr[idx].metadata.size = 0;
    }

    fi->fh = (uint64_t)fd;
    fuse_reply_open(req, fi);
}

static void ll_release(fuse_req_t req, fuse_ino_t ino,
                       struct fuse_file_info* fi) {
    (void)ino;
    if (fi->fh) {
        close((int)fi->fh);
    }
    fuse_reply_err(req, 0);
}

static void ll_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                    struct fuse_file_info* fi) {
    treefile* tf = get_treefile(req);
    int idx = ino_to_index(ino);

    if (idx < 0 || idx >= tf->size || tf->arr[idx].isdeleted) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    if (S_ISDIR(tf->arr[idx].metadata.mode)) {
        fuse_reply_err(req, EISDIR);
        return;
    }

    // Allocate read buffer
    char* buf = (char*)malloc(size);
    if (!buf) {
        fuse_reply_err(req, ENOMEM);
        return;
    }

    int fd = (int)fi->fh;
    ssize_t nread = pread(fd, buf, size, off);
    if (nread < 0) {
        free(buf);
        fuse_reply_err(req, errno);
        return;
    }

    fuse_reply_buf(req, buf, (size_t)nread);
    free(buf);
}

static void ll_write(fuse_req_t req, fuse_ino_t ino, const char* buf,
                     size_t size, off_t off, struct fuse_file_info* fi) {
    treefile* tf = get_treefile(req);
    int idx = ino_to_index(ino);

    if (idx < 0 || idx >= tf->size || tf->arr[idx].isdeleted) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    if (S_ISDIR(tf->arr[idx].metadata.mode)) {
        fuse_reply_err(req, EISDIR);
        return;
    }

    int fd = (int)fi->fh;
    ssize_t nwritten = pwrite(fd, buf, size, off);
    if (nwritten < 0) {
        fuse_reply_err(req, errno);
        return;
    }

    // Update file size in metadata
    off_t new_end = off + nwritten;
    if (new_end > tf->arr[idx].metadata.size) {
        tf->arr[idx].metadata.size = new_end;
    }

    tf->arr[idx].metadata.mtime = time(nullptr);
    tf->arr[idx].metadata.ctime = time(nullptr);

    // Notify dedup thread of file update (Operation: 2)
    send_dedup_request(ino, string(tf->arr[idx].metadata.name), 2);

    fuse_reply_write(req, (size_t)nwritten);
}

static void ll_opendir(fuse_req_t req, fuse_ino_t ino,
                       struct fuse_file_info* fi) {
    treefile* tf = get_treefile(req);
    int idx = ino_to_index(ino);

    if (idx < 0 || idx >= tf->size || tf->arr[idx].isdeleted) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    if (!S_ISDIR(tf->arr[idx].metadata.mode)) {
        fuse_reply_err(req, ENOTDIR);
        return;
    }

    fi->fh = 0; // No state needed
    fuse_reply_open(req, fi);
}

static void ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                       struct fuse_file_info* fi) {
    (void)fi;
    treefile* tf = get_treefile(req);
    int idx = ino_to_index(ino);

    if (idx < 0 || idx >= tf->size || tf->arr[idx].isdeleted) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    // Build a list of entries: ".", "..", then children
    struct dir_entry {
        string name;
        struct stat st;
    };
    vector<dir_entry> entries;

    // "."
    {
        dir_entry de;
        de.name = ".";
        fill_stat(&de.st, tf->arr[idx], idx);
        entries.push_back(de);
    }
    // ".."
    {
        int parent = tf->arr[idx].parent;
        if (parent < 0) parent = idx; // root's parent is itself
        dir_entry de;
        de.name = "..";
        fill_stat(&de.st, tf->arr[parent], parent);
        entries.push_back(de);
    }
    // Children
    {
        int child = tf->arr[idx].firstchild;
        int safety = tf->size;
        while (child >= 0 && child < tf->size && safety-- > 0) {
            if (!tf->arr[child].isdeleted) {
                dir_entry de;
                de.name = basename_of(tf->arr[child].metadata.name);
                fill_stat(&de.st, tf->arr[child], child);
                entries.push_back(de);
            }
            child = tf->arr[child].nextsibling;
        }
    }

    // Fill the reply buffer starting from offset 'off'
    char* buf = (char*)calloc(1, size);
    if (!buf) {
        fuse_reply_err(req, ENOMEM);
        return;
    }

    size_t buf_used = 0;
    for (size_t i = (size_t)off; i < entries.size(); i++) {
        size_t entry_size = fuse_add_direntry(req, buf + buf_used,
                                               size - buf_used,
                                               entries[i].name.c_str(),
                                               &entries[i].st,
                                               (off_t)(i + 1));
        if (entry_size > size - buf_used) {
            // Buffer full, stop here
            break;
        }
        buf_used += entry_size;
    }

    fuse_reply_buf(req, buf, buf_used);
    free(buf);
}

static void ll_releasedir(fuse_req_t req, fuse_ino_t ino,
                          struct fuse_file_info* fi) {
    (void)ino;
    (void)fi;
    fuse_reply_err(req, 0);
}

static void ll_statfs(fuse_req_t req, fuse_ino_t ino) {
    (void)ino;
    treefile* tf = get_treefile(req);

    struct statvfs st;
    memset(&st, 0, sizeof(st));
    st.f_bsize = 4096;
    st.f_frsize = 4096;
    st.f_blocks = (fsblkcnt_t)TREEFILE_MAX_NODES;
    st.f_bfree = (fsblkcnt_t)(TREEFILE_MAX_NODES - tf->nodeallocated);
    st.f_bavail = st.f_bfree;
    st.f_files = (fsfilcnt_t)TREEFILE_MAX_NODES;
    st.f_ffree = (fsfilcnt_t)(TREEFILE_MAX_NODES - tf->nodeallocated);
    st.f_favail = st.f_ffree;
    st.f_namemax = 255;

    fuse_reply_statfs(req, &st);
}

static void ll_rename(fuse_req_t req, fuse_ino_t parent, const char* name,
                      fuse_ino_t newparent, const char* newname,
                      unsigned int flags) {
    (void)flags;
    treefile* tf = get_treefile(req);
    int parent_idx = ino_to_index(parent);
    int newparent_idx = ino_to_index(newparent);

    if (parent_idx < 0 || parent_idx >= tf->size ||
        tf->arr[parent_idx].isdeleted ||
        newparent_idx < 0 || newparent_idx >= tf->size ||
        tf->arr[newparent_idx].isdeleted) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    // Find source
    int src_idx = find_child_by_name(parent_idx, name, *tf);
    if (src_idx < 0) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    // If target exists, remove it first
    int dst_idx = find_child_by_name(newparent_idx, newname, *tf);
    if (dst_idx >= 0) {
        if (S_ISDIR(tf->arr[dst_idx].metadata.mode)) {
            if (!dir_is_empty(dst_idx, *tf)) {
                fuse_reply_err(req, ENOTEMPTY);
                return;
            }
        }
        // Remove host-FS file if regular file
        if (S_ISREG(tf->arr[dst_idx].metadata.mode)) {
            unlink(host_data_path(dst_idx).c_str());
        }
        // Delete by full path
        string dst_path(tf->arr[dst_idx].metadata.name);
        delete1(dst_path, *tf);
    }

    // Remove old hashmap entry (full path)
    string old_path = build_full_path(src_idx, *tf);
    hashmap_remove(&tf->hashdata, old_path.c_str());

    // Unlink from old parent's sibling list (O(1) via prevsibling)
    {
        int prev = tf->arr[src_idx].prevsibling;
        int next = tf->arr[src_idx].nextsibling;
        if (prev >= 0 && prev < tf->size) {
            tf->arr[prev].nextsibling = next;
        } else {
            tf->arr[parent_idx].firstchild = next;
        }
        if (next >= 0 && next < tf->size) {
            tf->arr[next].prevsibling = prev;
        }
    }

    // Build new full path
    string new_parent_path = build_full_path(newparent_idx, *tf);
    string new_full_path;
    if (new_parent_path == "/") {
        new_full_path = "/" + string(newname);
    } else {
        new_full_path = new_parent_path + "/" + string(newname);
    }

    // Update the name to new full path
    strncpy(tf->arr[src_idx].metadata.name, new_full_path.c_str(), 299);
    tf->arr[src_idx].metadata.name[299] = '\0';

    // Prepend to new parent's child list
    int old_first = tf->arr[newparent_idx].firstchild;
    tf->arr[newparent_idx].firstchild = src_idx;
    tf->arr[src_idx].nextsibling = old_first;
    tf->arr[src_idx].prevsibling = -1;
    if (old_first >= 0 && old_first < tf->size) {
        tf->arr[old_first].prevsibling = src_idx;
    }
    tf->arr[src_idx].parent = newparent_idx;

    // Update nlink counts for directory moves
    if (S_ISDIR(tf->arr[src_idx].metadata.mode) && parent_idx != newparent_idx) {
        if (tf->arr[parent_idx].metadata.nlink > 2)
            tf->arr[parent_idx].metadata.nlink--;
        tf->arr[newparent_idx].metadata.nlink++;
    }

    // Add new hashmap entry (full path)
    hashmap_set(&tf->hashdata, new_full_path.c_str(), src_idx);

    tf->arr[src_idx].metadata.ctime = time(nullptr);

    fuse_reply_err(req, 0);
}

// ============================================================
// Ops struct assembly
// ============================================================

struct fuse_lowlevel_ops get_fuse_ll_ops() {
    struct fuse_lowlevel_ops ops;
    memset(&ops, 0, sizeof(ops));

    ops.init       = ll_init;
    ops.destroy    = ll_destroy;
    ops.lookup     = ll_lookup;
    ops.forget     = ll_forget;
    ops.getattr    = ll_getattr;
    ops.setattr    = ll_setattr;
    ops.mkdir      = ll_mkdir;
    ops.unlink     = ll_unlink;
    ops.rmdir      = ll_rmdir;
    ops.create     = ll_create;
    ops.open       = ll_open;
    ops.release    = ll_release;
    ops.read       = ll_read;
    ops.write      = ll_write;
    ops.opendir    = ll_opendir;
    ops.readdir    = ll_readdir;
    ops.releasedir = ll_releasedir;
    ops.statfs     = ll_statfs;
    ops.rename     = ll_rename;

    return ops;
}
