#ifndef FS_ACCESS_H
#define FS_ACCESS_H

#include "fuse_common.h"

int fs_access(const char *path, int mask);

#endif // FS_ACCESS_H
