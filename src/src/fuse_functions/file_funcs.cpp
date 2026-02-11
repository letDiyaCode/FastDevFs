#include "fuse_functions/file_funcs.h"
#include "daemon/dir_manager.h"
#include "daemon/file_io.h"

#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

extern DirManager* g_dir_manager;

/* CREATE */
int fdfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    int node = insert_node(g_dir_manager, path);
    if (node < 0) {
        std::cout << "fdfs_create: returning -EEXIST" << std::endl;
        return -EEXIST;
    }

    pthread_rwlock_wrlock(&g_dir_manager->rwlock);

    DirNode *n = &g_dir_manager->nodes[node];
    n->mode  = S_IFREG | (mode & 0777);
    n->uid   = getuid();
    n->gid   = getgid();
    n->size  = 0;
    n->nlink = 1;

    time_t now = time(NULL);
    n->atime = n->mtime = n->ctime = now;

    pthread_rwlock_unlock(&g_dir_manager->rwlock);
    std::cout << "fdfs_create: returning 0" << std::endl;
    return 0;
}

/* OPEN */
int fdfs_open(const char *path, struct fuse_file_info *fi) {
    pthread_rwlock_rdlock(&g_dir_manager->rwlock);

    int node = lookup_node_nolock(g_dir_manager, path);
    if (node < 0) {
        pthread_rwlock_unlock(&g_dir_manager->rwlock);
        std::cout << "fdfs_open: returning -ENOENT" << std::endl;
        return -ENOENT;
    }

    DirNode *n = &g_dir_manager->nodes[node];
    if (S_ISDIR(n->mode)) {
        pthread_rwlock_unlock(&g_dir_manager->rwlock);
        std::cout << "fdfs_open: returning -EISDIR" << std::endl;
        return -EISDIR;
    }

    pthread_rwlock_unlock(&g_dir_manager->rwlock);
    std::cout << "fdfs_open: returning 0" << std::endl;
    return 0;
}

/* READ */
int fdfs_read(const char *path, char *buf, size_t size,
              off_t offset, struct fuse_file_info *fi) {
    pthread_rwlock_rdlock(&g_dir_manager->rwlock);

    int node = lookup_node_nolock(g_dir_manager, path);
    if (node < 0) {
        pthread_rwlock_unlock(&g_dir_manager->rwlock);
        std::cout << "fdfs_read: returning -ENOENT" << std::endl;
        return -ENOENT;
    }

    DirNode *n = &g_dir_manager->nodes[node];
    if (offset >= n->size) {
        pthread_rwlock_unlock(&g_dir_manager->rwlock);
        std::cout << "fdfs_read: returning 0 (offset >= size)" << std::endl;
        return 0;
    }

    // Calculate how much data to read
    size_t to_read = size;
    if (offset + to_read > n->size) {
        to_read = n->size - offset;
    }

    pthread_rwlock_unlock(&g_dir_manager->rwlock);
    
    // Read data from host filesystem using inode number
    ssize_t bytes_read = read_inode_data(node, buf, to_read, offset);
    if (bytes_read < 0) {
        std::cout << "fdfs_read: returning -EIO" << std::endl;
        return -EIO;
    }

    // Update access time
    pthread_rwlock_wrlock(&g_dir_manager->rwlock);
    n = &g_dir_manager->nodes[node];
    n->atime = time(NULL);
    pthread_rwlock_unlock(&g_dir_manager->rwlock);
    
    std::cout << "fdfs_read: returning " << bytes_read << std::endl;
    return bytes_read;
}

/* WRITE */
int fdfs_write(const char *path, const char *buf, size_t size,
               off_t offset, struct fuse_file_info *fi) {

    // if (g_config.dedup_enabled)
    // {
    //     // call dedup logic
    // }
    // else
    // {
    //     // normal write_inode_data()
    // }

    pthread_rwlock_wrlock(&g_dir_manager->rwlock);
    std::cout << "fdfs_write: writing " << size << " bytes to " << path << " at offset " << offset << "data: "<<buf<<std::endl;
    int node = lookup_node_nolock(g_dir_manager, path);
    if (node < 0) {
        pthread_rwlock_unlock(&g_dir_manager->rwlock);
        std::cout << "fdfs_write: returning -ENOENT" << std::endl;
        return -ENOENT;
    }

    pthread_rwlock_unlock(&g_dir_manager->rwlock);
    
    // Write data to host filesystem using inode number
    ssize_t bytes_written = write_inode_data(node, buf, size, offset);
    if (bytes_written < 0) {
        std::cout << "fdfs_write: returning -EIO" << std::endl;
        return -EIO;
    }
    
    // Update file size and modification time
    pthread_rwlock_wrlock(&g_dir_manager->rwlock);
    DirNode *n = &g_dir_manager->nodes[node];
    
    off_t end = offset + size;
    if (end > n->size)
        n->size = end;

    n->mtime = n->ctime = time(NULL);
    pthread_rwlock_unlock(&g_dir_manager->rwlock);
    
    std::cout << "fdfs_write: returning " << bytes_written << std::endl;
    return bytes_written;
}

/* TRUNCATE */
int fdfs_truncate(const char *path, off_t size,
                  struct fuse_file_info *fi) {
    pthread_rwlock_wrlock(&g_dir_manager->rwlock);

    int node = lookup_node_nolock(g_dir_manager, path);
    if (node < 0) {
        pthread_rwlock_unlock(&g_dir_manager->rwlock);
        std::cout << "fdfs_truncate: returning -ENOENT" << std::endl;
        return -ENOENT;
    }

    pthread_rwlock_unlock(&g_dir_manager->rwlock);
    
    // Truncate the data file on host filesystem
    if (truncate_inode_data(node, size) < 0) {
        std::cout << "fdfs_truncate: returning -EIO" << std::endl;
        return -EIO;
    }
    
    // Update file size and modification time
    pthread_rwlock_wrlock(&g_dir_manager->rwlock);
    DirNode *n = &g_dir_manager->nodes[node];
    n->size = size;
    n->mtime = n->ctime = time(NULL);
    pthread_rwlock_unlock(&g_dir_manager->rwlock);
    
    std::cout << "fdfs_truncate: returning 0" << std::endl;
    return 0;
}

/* UNLINK */
int fdfs_unlink(const char *path) {
    pthread_rwlock_wrlock(&g_dir_manager->rwlock);

    int node = lookup_node_nolock(g_dir_manager, path);
    if (node < 0) {
        pthread_rwlock_unlock(&g_dir_manager->rwlock);
        std::cout << "fdfs_unlink: returning -ENOENT" << std::endl;
        return -ENOENT;
    }

    pthread_rwlock_unlock(&g_dir_manager->rwlock);
    
    // Delete the data file from host filesystem
    delete_inode_data(node);

    if (!remove_node(g_dir_manager, path)) {
        std::cout << "fdfs_unlink: returning -ENOENT (remove failed)" << std::endl;
        return -ENOENT;
    }

    std::cout << "fdfs_unlink: returning 0" << std::endl;
    return 0;
}