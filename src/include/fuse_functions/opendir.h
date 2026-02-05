#ifndef FASTDEVFS_OPENDIR_H
#define FASTDEVFS_OPENDIR_H

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>

/*
 * opendir FUSE callback
 * Verifies directory existence using DirManager ADT
 */
int fdfs_opendir(const char* path,
                      struct fuse_file_info* fi);

#endif // FASTDEVFS_OPENDIR_H
