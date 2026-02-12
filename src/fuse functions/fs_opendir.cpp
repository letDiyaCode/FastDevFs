#include "../../include/fuse functions/fs_opendir.h"

int fs_opendir(const char *path, struct fuse_file_info *fi) {
    (void) fi;
    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    std::string path_str(path);
    int index = -1;

    if (path_str == "/") {
        index = 0;
    } else {
        index = hashindex(path_str, file1);
    }

    if (index == -1) {
        return -ENOENT;
    }

    if (!S_ISDIR(file1.arr[index].metadata.mode)) {
        return -ENOTDIR;
    }

    // Check read permissions
    metadate& meta = file1.arr[index].metadata;
    struct fuse_context* ctx = fuse_get_context();

    bool allowed = false;
    if (ctx->uid == 0) allowed = true;
    else if (ctx->uid == meta.uid) {
        if (meta.mode & S_IRUSR) allowed = true;
    } else if (ctx->gid == meta.gid) {
        if (meta.mode & S_IRGRP) allowed = true;
    } else {
        if (meta.mode & S_IROTH) allowed = true;
    }

    if (!allowed) return -EACCES;

    return 0;
}
