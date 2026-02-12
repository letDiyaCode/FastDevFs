#include "../../include/fuse functions/fs_statfs.h"

int fs_statfs(const char *path, struct statvfs *stbuf) {
    (void) path;
    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    stbuf->f_bsize = 4096; // 4KB blocks
    stbuf->f_frsize = 4096;
    stbuf->f_blocks = file1.head.size; // Total nodes
    stbuf->f_bfree = file1.head.size - file1.head.nodeallocated; // Free nodes
    stbuf->f_bavail = stbuf->f_bfree;
    stbuf->f_files = file1.head.size;
    stbuf->f_ffree = file1.head.size - file1.head.nodeallocated;
    stbuf->f_favail = stbuf->f_ffree;
    stbuf->f_namemax = 255;

    return 0;
}
