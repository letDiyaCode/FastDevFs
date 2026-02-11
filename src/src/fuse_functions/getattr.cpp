#include "fuse_functions/getattr.h"
#include "daemon/dir_manager.h"

#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <iostream>

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
    pthread_rwlock_rdlock(&g_dir_manager->rwlock);
    
    int node = lookup_node_nolock(g_dir_manager, path);
    if (node == -1) {
        pthread_rwlock_unlock(&g_dir_manager->rwlock);
        std::cout << "fdfs_getattr: returning -ENOENT" << std::endl;
        return -ENOENT;
    }

    // Get the actual node metadata
    DirNode* n = &g_dir_manager->nodes[node];
    
    // Fill stat structure with actual metadata from DirNode
    stbuf->st_mode  = n->mode;
    stbuf->st_nlink = n->nlink;
    stbuf->st_size  = n->size;
    stbuf->st_uid   = n->uid;
    stbuf->st_gid   = n->gid;
    stbuf->st_atime = n->atime;
    stbuf->st_mtime = n->mtime;
    stbuf->st_ctime = n->ctime;
    
    pthread_rwlock_unlock(&g_dir_manager->rwlock);

    std::cout << "fdfs_getattr: returning 0" << std::endl;
    return 0;
}
