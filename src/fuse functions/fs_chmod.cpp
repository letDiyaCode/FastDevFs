#include "../../include/fuse functions/fs_chmod.h"

int fs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
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

    // Only owner or superuser can chmod
    if (ctx->uid != 0 && ctx->uid != meta.uid) {
        return -EPERM;
    }

    // Preserve the file type bits (S_IFMT), update permission bits
    meta.mode = (meta.mode & S_IFMT) | (mode & 07777);
    meta.ctime = time(nullptr);

    persist(file1);
    return 0;
}
