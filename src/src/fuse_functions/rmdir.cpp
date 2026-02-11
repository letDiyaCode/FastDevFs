#include "fuse_functions/rmdir.h"
#include "daemon/dir_manager.h"

#include <cerrno>
#include <cstring>
#include <iostream>

/*
 * Global DirManager instance
 * (defined in main.cpp)
 */
extern DirManager* g_dir_manager;

int fdfs_rmdir(const char* path)
{
    // Disallow removing root
    if (strcmp(path, "/") == 0) {
        std::cout << "fdfs_rmdir: returning -EBUSY (root)" << std::endl;
        return -EBUSY;
    }

    /*
     * remove_node returns:
     *  - true  → directory removed
     *  - false → failure (non-empty, not found, etc.)
     */
    bool ok = remove_node(g_dir_manager, path);
    if (!ok) {
        // if (node == -1) {
        //     return -ENOENT;
        // }                      commented bcz of dead locking in lookup_node
        std::cout << "fdfs_rmdir: returning -ENOTEMPTY" << std::endl;
        return -ENOTEMPTY;
    }

    std::cout << "fdfs_rmdir: returning 0" << std::endl;
    return 0;
}
