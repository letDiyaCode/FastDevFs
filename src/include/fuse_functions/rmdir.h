#ifndef FASTDEVFS_RMDIR_H
#define FASTDEVFS_RMDIR_H

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>

/*
 * rmdir FUSE callback
 * Removes an empty directory using DirManager ADT
 */
int fdfs_rmdir(const char* path);

#endif // FASTDEVFS_RMDIR_H
