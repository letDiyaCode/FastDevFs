#ifndef FS_OPEN_H
#define FS_OPEN_H

#include "fuse_common.h"

int fs_open(const char *path, struct fuse_file_info *fi);

#endif // FS_OPEN_H
