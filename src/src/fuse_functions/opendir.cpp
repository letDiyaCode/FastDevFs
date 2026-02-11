#include "fuse_functions/opendir.h"
#include "daemon/dir_manager.h"

#include <cstring>
#include <cerrno>
#include <iostream>

/*
 * Global DirManager instance
 * (defined in main.cpp)
 */
extern DirManager* g_dir_manager;

int fdfs_opendir(const char* path,
                      struct fuse_file_info* fi)
{
    (void) fi;

    // Ask ADT whether directory exists
    int node = lookup_node(g_dir_manager, path);
    if (node == -1) {
        std::cout << "fdfs_opendir: returning -ENOENT" << std::endl;
        return -ENOENT;
    }

    std::cout << "fdfs_opendir: returning 0" << std::endl;
    return 0;
}
