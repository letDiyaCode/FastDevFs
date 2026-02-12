#include "../../include/fuse functions/fs_rmdir.h"

int fs_rmdir(const char *path) {
    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    std::string path_str(path);
    int index = hashindex(path_str, file1);

    if (index == -1) {
        return -ENOENT;
    }

    if (!S_ISDIR(file1.arr[index].metadata.mode)) {
        return -ENOTDIR;
    }

    // Security check: Write permission on parent
    int parent_idx = file1.arr[index].parent;
    if (parent_idx != -1) {
         metadate& parent_meta = file1.arr[parent_idx].metadata;
         struct fuse_context* ctx = fuse_get_context();
         bool allowed = false;
         if (ctx->uid == 0) allowed = true;
         else if (ctx->uid == parent_meta.uid && (parent_meta.mode & S_IWUSR)) allowed = true;
         else if (ctx->gid == parent_meta.gid && (parent_meta.mode & S_IWGRP)) allowed = true;
         else if (parent_meta.mode & S_IWOTH) allowed = true;
         
         if (!allowed) return -EACCES;
    }

    // Check if empty
    int child = file1.arr[index].firstchild;
    while (child != -1) {
        if (!file1.arr[child].isdeleted) {
            return -ENOTEMPTY;
        }
        child = file1.arr[child].nextsibling;
    }

    delete1(path_str, file1);
    
    return 0;
}
