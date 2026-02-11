#define FUSE_USE_VERSION 31

#pragma once

#include <fuse3/fuse.h>
#include <sys/types.h>

// file ops
int fdfs_open(const char *path, struct fuse_file_info *fi);
int fdfs_read(const char *path, char *buf, size_t size,
              off_t offset, struct fuse_file_info *fi);
int fdfs_write(const char *path, const char *buf, size_t size,
               off_t offset, struct fuse_file_info *fi);
int fdfs_create(const char *path, mode_t mode,
                struct fuse_file_info *fi);
int fdfs_unlink(const char *path);
int fdfs_truncate(const char *path, off_t size, fuse_file_info* fi);
