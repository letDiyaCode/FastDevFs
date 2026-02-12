#ifndef FS_WRITE_H
#define FS_WRITE_H

#include "fuse_common.h"

int fs_write(const char *path, const char *buf, size_t size, off_t offset,
             struct fuse_file_info *fi);

#endif // FS_WRITE_H
