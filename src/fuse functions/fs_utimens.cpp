#include "../../include/fuse functions/fs_utimens.h"

int fs_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
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

    // POSIX: owner or superuser can set arbitrary times.
    // Anyone with write permission can set times to "now" (tv==NULL or UTIME_NOW).
    bool is_owner = (ctx->uid == 0 || ctx->uid == meta.uid);
    if (!is_owner && tv != NULL) {
        // Non-owner setting explicit times: need write permission
        bool has_write = false;
        if (ctx->uid == meta.uid) {
            has_write = (meta.mode & S_IWUSR) != 0;
        } else if (ctx->gid == meta.gid) {
            has_write = (meta.mode & S_IWGRP) != 0;
        } else {
            has_write = (meta.mode & S_IWOTH) != 0;
        }
        if (!has_write) return -EPERM;
    }

    if (tv) {
        meta.atime = tv[0].tv_sec;
        meta.mtime = tv[1].tv_sec;
    } else {
        time_t now = time(nullptr);
        meta.atime = now;
        meta.mtime = now;
    }

    meta.ctime = time(nullptr);

    persist(file1);
    return 0;
}
