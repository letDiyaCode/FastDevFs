#include "../../include/fuse functions/fs_getattr.h"
#include <cstring>

int fs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    // Initialize stbuf
    memset(stbuf, 0, sizeof(struct stat));

    // Resolve path to index — root "/" is always at index 0
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
    stbuf->st_ino  = index;  // Use array index as inode number
    stbuf->st_mode = meta.mode;
    stbuf->st_nlink = meta.nlink;
    stbuf->st_uid = meta.uid;
    stbuf->st_gid = meta.gid;
    stbuf->st_size = meta.size;
    stbuf->st_atime = meta.atime;
    stbuf->st_mtime = meta.mtime;
    stbuf->st_ctime = meta.ctime;

    return 0;
}
