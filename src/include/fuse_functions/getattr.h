#ifndef FASTDEVFS_GETATTR_H
#define FASTDEVFS_GETATTR_H

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>

int fastdevfs_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi);

#endif