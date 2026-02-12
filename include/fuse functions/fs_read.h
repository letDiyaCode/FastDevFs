#ifndef FS_READ_H
#define FS_READ_H

#include "fuse_common.h"

int fs_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi);

#endif // FS_READ_H
