#include "fuse_functions/readdir.h"
#include "daemon/dir_manager.h"

#include <cstring>
#include <cerrno>
#include <iostream>
/*
 * Global DirManager instance
 * (defined in main.cpp)
 */
extern DirManager* g_dir_manager;

int fdfs_readdir(const char* path,
                      void* buf,
                      fuse_fill_dir_t filler,
                      off_t offset,
                      struct fuse_file_info* fi,
                      enum fuse_readdir_flags flags)
{
    (void) offset;
    (void) fi;
    (void) flags;
    // Find directory node
    int dir_node = lookup_node(g_dir_manager, path);
    if (dir_node == -1) {
        std::cout << "fdfs_readdir: returning -ENOENT" << std::endl;
        return -ENOENT;
    }

    // Every directory must contain "." and ".."
    filler(buf, ".",  nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "..", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));

    // Iterate over children using sibling list
    int child = (*g_dir_manager).nodes[dir_node].first_child;
    while (child != -1) {
        DirNode* n = &g_dir_manager->nodes[child];

        if (n->in_use) {
            filler(buf, n->name, nullptr, 0,
                   static_cast<fuse_fill_dir_flags>(0));
        }

        child = n->next_sibling;
        // std::cout<<"I) am Here";
        // std::cout.flush();
    }

    std::cout << "fdfs_readdir: returning 0" << std::endl;
    return 0;
}
