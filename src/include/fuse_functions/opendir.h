#ifndef FASTDEVFS_OPENDIR_H
#define FASTDEVFS_OPENDIR_H

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>

int fastdevfs_opendir(
    const char* path,
    struct fuse_file_info* fi
);

#endif
