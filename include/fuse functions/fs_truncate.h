#ifndef FS_TRUNCATE_H
#define FS_TRUNCATE_H

#include "fuse_common.h"

int fs_truncate(const char *path, off_t size, struct fuse_file_info *fi);

#endif // FS_TRUNCATE_H
