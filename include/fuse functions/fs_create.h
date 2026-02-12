#ifndef FS_CREATE_H
#define FS_CREATE_H

#include "fuse_common.h"

int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi);

#endif // FS_CREATE_H
