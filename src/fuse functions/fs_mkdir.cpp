#include "../../include/fuse functions/fs_mkdir.h"

int fs_mkdir(const char *path, mode_t mode) {
    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    std::string path_str(path);
    std::string parent_path = get_parent_path(path_str);
    
    // Check if parent exists and has write permissions
    int parent_index = -1;
    if (parent_path == "/" || parent_path.empty()) {
        parent_index = 0; // Root
    } else {
        parent_index = hashindex(parent_path, file1);
    }

    if (parent_index == -1) {
        return -ENOENT;
    }

    // Security check: Write permission on parent
    metadate& parent_meta = file1.arr[parent_index].metadata;
    struct fuse_context* ctx = fuse_get_context();
    
    // Root (index 0) special case - might not have full metadata init if not explicitly set?
    // Assuming initialized correctly.
    if (parent_index == 0 && parent_meta.mode == 0) {
       // Fallback if root metadata isn't fully set (though adt should set it)
    } else {
        bool allowed = false;
        if (ctx->uid == 0) allowed = true; // Root always allowed
        else if (ctx->uid == parent_meta.uid) {
            if (parent_meta.mode & S_IWUSR) allowed = true;
        } else if (ctx->gid == parent_meta.gid) {
            if (parent_meta.mode & S_IWGRP) allowed = true;
        } else {
            if (parent_meta.mode & S_IWOTH) allowed = true;
        }
        if (!allowed) return -EACCES;
    }

    // Check if directory already exists
    if (hashindex(path_str, file1) != -1) {
        return -EEXIST;
    }

    insertfolder(path_str, parent_path, file1);
    
    // Check if insertion succeeded
    int new_index = hashindex(path_str, file1);
    if (new_index == -1) {
        return -ENOMEM; // Or other error
    }

    // Update metadata with requested mode and owner
    file1.arr[new_index].metadata.mode = (mode & 0777) | S_IFDIR;
    file1.arr[new_index].metadata.uid = ctx->uid;
    file1.arr[new_index].metadata.gid = ctx->gid;

    return 0;
}
