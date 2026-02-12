#ifndef FS_UTIMENS_H
#define FS_UTIMENS_H

#include "fuse_common.h"

int fs_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi);

#endif // FS_UTIMENS_H
