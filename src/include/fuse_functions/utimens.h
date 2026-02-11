#ifndef FDFS_UTIMENS_H
#define FDFS_UTIMENS_H

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>

/*
 * Update file times (atime, mtime)
 * Called by touch and similar utilities
 */
int fdfs_utimens(const char* path,
                 const struct timespec tv[2],
                 struct fuse_file_info* fi);

#endif // FDFS_UTIMENS_H
