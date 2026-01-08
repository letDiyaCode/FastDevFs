#ifndef FASTDEVFS_READDIR_H
#define FASTDEVFS_READDIR_H

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>

int fastdevfs_readdir(
    const char* path,
    void* buf,
    fuse_fill_dir_t filler,
    off_t offset,
    struct fuse_file_info* fi,
    enum fuse_readdir_flags flags
);

#endif
