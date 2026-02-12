#ifndef FS_MKDIR_H
#define FS_MKDIR_H

#include "fuse_common.h"

int fs_mkdir(const char *path, mode_t mode);

#endif // FS_MKDIR_H
