#include "fuse_functions/utimens.h"
#include "daemon/dir_manager.h"

#include <cerrno>
#include <iostream>
#include <time.h>

/*
 * Global DirManager instance
 */
extern DirManager* g_dir_manager;

int fdfs_utimens(const char* path,
                 const struct timespec tv[2],
                 struct fuse_file_info* fi)
{
    (void) fi;
    
    pthread_rwlock_wrlock(&g_dir_manager->rwlock);
    
    int node = lookup_node_nolock(g_dir_manager, path);
    if (node < 0) {
        pthread_rwlock_unlock(&g_dir_manager->rwlock);
        std::cout << "fdfs_utimens: returning -ENOENT" << std::endl;
        return -ENOENT;
    }
    
    DirNode* n = &g_dir_manager->nodes[node];
    
    // If tv is NULL, use current time
    if (tv == nullptr) {
        time_t now = time(NULL);
        n->atime = now;
        n->mtime = now;
    } else {
        // tv[0] is access time, tv[1] is modification time
        n->atime = tv[0].tv_sec;
        n->mtime = tv[1].tv_sec;
    }
    
    pthread_rwlock_unlock(&g_dir_manager->rwlock);
    std::cout << "fdfs_utimens: returning 0" << std::endl;
    return 0;
}
