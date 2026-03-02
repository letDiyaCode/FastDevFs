#include "../../include/fuse functions/fs_create.h"

int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    std::string path_str(path);
    std::string parent_path = get_parent_path(path_str);
    
    // Resolve parent directory
    int parent_index = -1;
    if (parent_path == "/" || parent_path.empty()) {
        parent_index = 0;
    } else {
        parent_index = hashindex(parent_path, file1);
    }

    if (parent_index == -1) {
        return -ENOENT;
    }

    // Check write+execute permission on parent directory
    metadate& parent_meta = file1.arr[parent_index].metadata;
    struct fuse_context* ctx = fuse_get_context();

    bool allowed = false;
    if (ctx->uid == 0) allowed = true;  // Superuser always allowed
    else if (ctx->uid == parent_meta.uid) {
        if ((parent_meta.mode & S_IWUSR) && (parent_meta.mode & S_IXUSR)) allowed = true;
    } else if (ctx->gid == parent_meta.gid) {
        if ((parent_meta.mode & S_IWGRP) && (parent_meta.mode & S_IXGRP)) allowed = true;
    } else {
        if ((parent_meta.mode & S_IWOTH) && (parent_meta.mode & S_IXOTH)) allowed = true;
    }
    if (!allowed) return -EACCES;

    // Check if file already exists
    int existing = hashindex(path_str, file1);
    if (existing != -1) {
        // O_EXCL → must fail
        if (fi && (fi->flags & O_EXCL)) {
            return -EEXIST;
        }
        // Re-open the existing file; apply O_TRUNC if requested
        if (fi && (fi->flags & O_TRUNC)) {
            metadate& meta = file1.arr[existing].metadata;
            size_t clear = ((size_t)meta.size < MAX_FILE_DATA)
                           ? (size_t)meta.size : MAX_FILE_DATA;
            if (clear > 0) {
                memset(file1.arr[existing].data, 0, clear);
            }
            meta.size = 0;
            meta.mtime = time(NULL);
            meta.ctime = time(NULL);
            persist(file1);
        }
        return 0;  // success — file already exists, just re-opened
    }

    insertfile(path_str, parent_path, file1);
    
    int new_index = hashindex(path_str, file1);
    if (new_index == -1) {
        return -ENOMEM;
    }

    // Set metadata with requested mode and caller's uid/gid
    metadate& meta = file1.arr[new_index].metadata;
    meta.mode = (mode & 07777) | S_IFREG;
    meta.uid = ctx->uid;
    meta.gid = ctx->gid;
    meta.size = 0;
    // data buffer is already zeroed by insertfile()
    
    persist(file1);
    return 0;
}
