#include "../../include/fuse functions/fs_unlink.h"

int fs_unlink(const char *path) {
    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    std::string path_str(path);
    int index = hashindex(path_str, file1);

    if (index == -1) {
        return -ENOENT;
    }

    // Check if it's a directory? unlink usually fails on directories (use rmdir)
    if (S_ISDIR(file1.arr[index].metadata.mode)) {
        return -EISDIR;
    }

    // Check parent permissions
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

    delete1(path_str, file1);

    persist(file1);
    return 0;
}
