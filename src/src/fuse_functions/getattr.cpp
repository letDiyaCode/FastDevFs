#include "fuse_functions/getattr.h"
#include "daemon/dir_manager.h"

#include <cstring>
#include <cerrno>
#include <unistd.h>

/*
 * DirManager instance
 * (defined in main.cpp and shared across FUSE callbacks)
 */
extern DirManager* g_dir_manager;

int fdfs_getattr(const char* path,
                      struct stat* stbuf,
                      struct fuse_file_info* fi)
{
    (void) fi;
    memset(stbuf, 0, sizeof(struct stat));

    // Ask DirManager if path exists
    int node = lookup_node(g_dir_manager, path);
    if (node == -1) {
        return -ENOENT;
    }

    // Currently, everything is a directory
    stbuf->st_mode  = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    stbuf->st_size = 4096;
    stbuf->st_uid   = getuid();
    stbuf->st_gid   = getgid();

    return 0;
}
