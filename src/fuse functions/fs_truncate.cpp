#include "../../include/fuse functions/fs_truncate.h"

int fs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    (void) fi;
    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    int index = hashindex(path, file1);
    if (index == -1) {
        return -ENOENT;
    }

    metadate& meta = file1.arr[index].metadata;
    struct fuse_context* ctx = fuse_get_context();

    // Check write permissions
    bool allowed = false;
    if (ctx->uid == 0) allowed = true;
    else if (ctx->uid == meta.uid && (meta.mode & S_IWUSR)) allowed = true;
    else if (ctx->gid == meta.gid && (meta.mode & S_IWGRP)) allowed = true;
    else if (meta.mode & S_IWOTH) allowed = true;

    if (!allowed) return -EACCES;

    // Update size
    meta.size = size;
    meta.mtime = time(NULL);
    meta.ctime = time(NULL);

    return 0;
}
