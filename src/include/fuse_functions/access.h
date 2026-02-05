#ifndef FASTDEVFS_ACCESS_H
#define FASTDEVFS_ACCESS_H

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>

/*
 * access FUSE callback
 * Checks existence and basic permissions
 */
int fdfs_access(const char* path, int mask);

#endif // FASTDEVFS_ACCESS_H
