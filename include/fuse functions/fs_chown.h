#ifndef FS_CHOWN_H
#define FS_CHOWN_H

#include "fuse_common.h"

int fs_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi);

#endif // FS_CHOWN_H
