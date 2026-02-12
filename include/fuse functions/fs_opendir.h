#ifndef FS_OPENDIR_H
#define FS_OPENDIR_H

#include "fuse_common.h"

int fs_opendir(const char *path, struct fuse_file_info *fi);

#endif // FS_OPENDIR_H
