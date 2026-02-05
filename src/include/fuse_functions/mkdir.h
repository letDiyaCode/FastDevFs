#ifndef FASTDEVFS_MKDIR_H
#define FASTDEVFS_MKDIR_H

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>

/*
 * mkdir FUSE callback
 * Creates a directory using DirManager ADT
 */
int fdfs_mkdir(const char* path, mode_t mode);

#endif // FASTDEVFS_MKDIR_H
