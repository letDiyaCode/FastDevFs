#ifndef FASTDEVFS_STATFS_H
#define FASTDEVFS_STATFS_H

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>

/*
 * statfs FUSE callback
 * Reports filesystem statistics
 */
int fdfs_statfs(const char* path, struct statvfs* stbuf);

#endif // FASTDEVFS_STATFS_H
