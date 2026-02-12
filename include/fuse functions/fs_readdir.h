#ifndef FS_READDIR_H
#define FS_READDIR_H

#include "fuse_common.h"

int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi,
               enum fuse_readdir_flags flags);

#endif // FS_READDIR_H
