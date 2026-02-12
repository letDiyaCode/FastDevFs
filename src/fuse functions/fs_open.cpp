#include "../../include/fuse functions/fs_open.h"

int fs_open(const char *path, struct fuse_file_info *fi) {
    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    std::string path_str(path);
    int index = hashindex(path_str, file1);

    if (index == -1) {
        return -ENOENT;
    }

    metadate& meta = file1.arr[index].metadata;
    
    // Check permissions based on fi->flags
    int flags = fi->flags & O_ACCMODE;
    int mode = meta.mode;
    struct fuse_context* ctx = fuse_get_context();

    bool r_ok = false;
    bool w_ok = false;
    
    if (ctx->uid == 0) {
        r_ok = true; w_ok = true;
    } else if (ctx->uid == meta.uid) {
        if (mode & S_IRUSR) r_ok = true;
        if (mode & S_IWUSR) w_ok = true;
    } else if (ctx->gid == meta.gid) {
        if (mode & S_IRGRP) r_ok = true;
        if (mode & S_IWGRP) w_ok = true;
    } else {
        if (mode & S_IROTH) r_ok = true;
        if (mode & S_IWOTH) w_ok = true;
    }

    if ((flags == O_RDONLY) && !r_ok) return -EACCES;
    if ((flags == O_WRONLY) && !w_ok) return -EACCES;
    if ((flags == O_RDWR) && (!r_ok || !w_ok)) return -EACCES;

    return 0;
}
