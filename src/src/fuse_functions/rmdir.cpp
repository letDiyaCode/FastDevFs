#include "fuse_functions/rmdir.h"
#include "daemon/dir_manager.h"

#include <cerrno>
#include <cstring>

/*
 * Global DirManager instance
 * (defined in main.cpp)
 */
extern DirManager* g_dir_manager;

int fdfs_rmdir(const char* path)
{
    // Disallow removing root
    if (strcmp(path, "/") == 0) {
        return -EBUSY;
    }

    /*
     * remove_node returns:
     *  - true  → directory removed
     *  - false → failure (non-empty, not found, etc.)
     */
    bool ok = remove_node(g_dir_manager, path);
    if (!ok) {
        // int node = lookup_node(&g_dir_manager, path);
        // if (node == -1) {
        //     return -ENOENT;
        // }                      commented bcz of dead locking in lookup_node
        return -ENOTEMPTY;
    }

    return 0;
}
