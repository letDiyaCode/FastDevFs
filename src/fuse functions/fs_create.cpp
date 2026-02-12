#include "../../include/fuse functions/fs_create.h"

int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void) fi;
    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    std::string path_str(path);
    std::string parent_path = get_parent_path(path_str);
    
    // Check if parent exists
    int parent_index = -1;
    if (parent_path == "/" || parent_path.empty()) {
        parent_index = 0;
    } else {
        parent_index = hashindex(parent_path, file1);
    }

    if (parent_index == -1) {
        return -ENOENT;
    }

    // Security check: Write permission on parent
    metadate& parent_meta = file1.arr[parent_index].metadata;
    struct fuse_context* ctx = fuse_get_context();
    
    // Permissions check
    bool allowed = false;
    if (ctx->uid == 0) allowed = true;
    else if (ctx->uid == parent_meta.uid && (parent_meta.mode & S_IWUSR)) allowed = true;
    else if (ctx->gid == parent_meta.gid && (parent_meta.mode & S_IWGRP)) allowed = true;
    else if (parent_meta.mode & S_IWOTH) allowed = true;

    if (!allowed && parent_index != 0) { // Root usually writeable in this context?
        return -EACCES;
    }
    
    // Create file
    if (hashindex(path_str, file1) != -1) {
        return -EEXIST;
    }

    insertfile(path_str, parent_path, file1);
    
    int new_index = hashindex(path_str, file1);
    if (new_index == -1) {
        return -ENOMEM;
    }

    metadate& meta = file1.arr[new_index].metadata;
    meta.mode = (mode & 0777) | S_IFREG;
    meta.uid = ctx->uid;
    meta.gid = ctx->gid;
    meta.size = 0;
    
    // Update parent mtime?
    // file1.arr[parent_index].metadata.mtime = time(NULL); 
    // Not stricly required by assignment but good practice.
    
    return 0;
}
