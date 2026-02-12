#include "../../include/fuse functions/fs_access.h"

int fs_access(const char *path, int mask) {
    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    std::string path_str(path);
    int index = hashindex(path_str, file1);

    if (index == -1) {
        return -ENOENT;
    }

    metadate& meta = file1.arr[index].metadata;
    struct fuse_context* ctx = fuse_get_context();

    // If root, always accessible?
    // Check permissions
    int mode = meta.mode;
    
    // Check owner
    if (ctx->uid == meta.uid) {
        if ((mask & R_OK) && !(mode & S_IRUSR)) return -EACCES;
        if ((mask & W_OK) && !(mode & S_IWUSR)) return -EACCES;
        if ((mask & X_OK) && !(mode & S_IXUSR)) return -EACCES;
    }
    // Check group
    else if (ctx->gid == meta.gid) {
        if ((mask & R_OK) && !(mode & S_IRGRP)) return -EACCES;
        if ((mask & W_OK) && !(mode & S_IWGRP)) return -EACCES;
        if ((mask & X_OK) && !(mode & S_IXGRP)) return -EACCES;
    }
    // Check others
    else {
        if ((mask & R_OK) && !(mode & S_IROTH)) return -EACCES;
        if ((mask & W_OK) && !(mode & S_IWOTH)) return -EACCES;
        if ((mask & X_OK) && !(mode & S_IXOTH)) return -EACCES;
    }

    return 0;
}
