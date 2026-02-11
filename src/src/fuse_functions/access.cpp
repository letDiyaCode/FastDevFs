#include "fuse_functions/access.h"
#include "daemon/dir_manager.h"

#include <cerrno>
#include <iostream>

/*
 * Global DirManager instance
 */
extern DirManager* g_dir_manager;

int fdfs_access(const char* path, int mask)
{
    (void) mask;  // permissions not enforced yet

    // Check if path exists
    int node = lookup_node(g_dir_manager, path);
    if (node == -1) {
        std::cout << "fdfs_access: returning -ENOENT" << std::endl;
        return -ENOENT;
    }

    // Allow all access for now
    std::cout << "fdfs_access: returning 0" << std::endl;
    return 0;
}
