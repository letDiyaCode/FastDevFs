#include "../../include/fuse functions/fs_utimens.h"

int fs_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
    (void) fi;
    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    int index = hashindex(path, file1);
    if (index == -1) {
        return -ENOENT;
    }

    metadate& meta = file1.arr[index].metadata;
    struct fuse_context* ctx = fuse_get_context();

    // Check ownership or write permissions
    // Owner can always change time. Root can.
    // Use of utimens usually requires owner or write access.
    bool allowed = false;
    if (ctx->uid == 0 || ctx->uid == meta.uid) allowed = true;
    else if (meta.mode & S_IWUSR && ctx->uid == meta.uid) allowed = true;
    // Actually, if we are not owner, we need write permission to file to update times
    else {
         // simplified check:
         if (ctx->uid == meta.uid) allowed = true;
         // TODO: Check full permissions if needed, but ownership is primary for utimens
    }

    if (!allowed && tv != NULL) {
         // If tv is NULL (update to now), write permission is enough.
         // If tv is NOT NULL, ownership is usually required.
         // Let's assume ownership required for setting specific time.
         return -EPERM;
    }
    
    // But wait, if tv is NULL, we just need write access?
    // The fuse logic might handle 'touch' calls where tv is NULL.
    // Tv passed here is const struct timespec tv[2].
    
    if (tv) {
        meta.atime = tv[0].tv_sec;
        meta.mtime = tv[1].tv_sec;
    } else {
        time_t now = time(nullptr);
        meta.atime = now;
        meta.mtime = now;
    }
    
    // ctime should update too
    meta.ctime = time(nullptr);

    return 0;
}
