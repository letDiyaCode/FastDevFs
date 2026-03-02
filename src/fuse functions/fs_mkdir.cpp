#include "../../include/fuse functions/fs_mkdir.h"

int fs_mkdir(const char *path, mode_t mode) {
    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    std::string path_str(path);
    std::string parent_path = get_parent_path(path_str);
    
    // Resolve parent directory
    int parent_index = -1;
    if (parent_path == "/" || parent_path.empty()) {
        parent_index = 0; // Root
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

    // Check if directory already exists
    if (hashindex(path_str, file1) != -1) {
        return -EEXIST;
    }

    insertfolder(path_str, parent_path, file1);
    
    // Check if insertion succeeded
    int new_index = hashindex(path_str, file1);
    if (new_index == -1) {
        return -ENOMEM;
    }

    // Update metadata with requested mode and caller's uid/gid
    file1.arr[new_index].metadata.mode = (mode & 07777) | S_IFDIR;
    file1.arr[new_index].metadata.uid = ctx->uid;
    file1.arr[new_index].metadata.gid = ctx->gid;

    persist(file1);
    return 0;
}
