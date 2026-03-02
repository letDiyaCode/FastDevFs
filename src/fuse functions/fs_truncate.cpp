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

    if (size < 0) return -EINVAL;
    if (size > MAX_FILE_DATA) return -EFBIG;

    off_t old_size = meta.size;

    if (size < old_size) {
        // Shrinking: zero out the truncated region
        size_t clear_from = (size_t)size;
        size_t clear_to = (old_size < MAX_FILE_DATA) ? (size_t)old_size : MAX_FILE_DATA;
        if (clear_from < clear_to) {
            memset(file1.arr[index].data + clear_from, 0, clear_to - clear_from);
        }
    } else if (size > old_size) {
        // Growing: zero-fill the gap (POSIX: reads as NUL)
        size_t fill_from = (old_size < MAX_FILE_DATA) ? (size_t)old_size : MAX_FILE_DATA;
        size_t fill_to = ((size_t)size < MAX_FILE_DATA) ? (size_t)size : MAX_FILE_DATA;
        if (fill_from < fill_to) {
            memset(file1.arr[index].data + fill_from, 0, fill_to - fill_from);
        }
    }

    meta.size = size;
    meta.mtime = time(NULL);
    meta.ctime = time(NULL);

    persist(file1);
    return 0;
}
