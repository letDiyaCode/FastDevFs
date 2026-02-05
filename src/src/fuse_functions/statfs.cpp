#include "fuse_functions/statfs.h"

#include <cstring>

/*
 * We return fake but consistent filesystem stats.
 * This is normal for in-memory / virtual filesystems.
 */
int fdfs_statfs(const char* path, struct statvfs* stbuf)
{
    (void) path;

    memset(stbuf, 0, sizeof(struct statvfs));

    stbuf->f_bsize   = 4096;        // block size
    stbuf->f_frsize  = 4096;        // fragment size

    stbuf->f_blocks  = 1024 * 1024; // total blocks (fake)
    stbuf->f_bfree   = 1024 * 1024; // free blocks
    stbuf->f_bavail  = 1024 * 1024; // available blocks

    stbuf->f_files   = 100000;      // total "inodes"
    stbuf->f_ffree   = 100000;      // free "inodes"

    stbuf->f_namemax = 255;         // max filename length

    return 0;
}
