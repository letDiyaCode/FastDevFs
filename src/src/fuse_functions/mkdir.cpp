#include "fuse_functions/mkdir.h"
#include "daemon/dir_manager.h"

#include <cstring>
#include <cerrno>

/*
 * Global DirManager instance
 * (defined in main.cpp)
 */
extern DirManager* g_dir_manager;

int fdfs_mkdir(const char* path, mode_t mode)
{
    (void) mode;  // permissions not handled yet

    // Disallow creating root
    if (strcmp(path, "/") == 0) {
        return -EEXIST;
    }

    /*
     * insert_node returns:
     *  - node index on success
     *  - -1 on failure
     */
    int node = insert_node(g_dir_manager, path);
    if (node == -1) {
        /*
         * Possible reasons:
         *  - parent does not exist
         *  - directory already exists
         *  - no free nodes left
         *
         * We conservatively return EEXIST or ENOENT.
         */
        if (lookup_node(g_dir_manager, path) != -1) {
            return -EEXIST;
        }
        return -ENOENT;
    }

    return 0;
}
