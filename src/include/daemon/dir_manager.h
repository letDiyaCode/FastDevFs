#pragma once

#include <cstdint>
#include <shared_mutex>

constexpr int FDFS_MAX_INODES      = 1024;
constexpr int FDFS_MAX_DIR_ENTRIES = 64;
constexpr int FDFS_MAX_NAME_LEN    = 64;

using inode_t = uint64_t;

/**
 * Directory Tree ADT
 * Static, thread-safe, no dynamic allocation
 */
class DirManager {
public:
    DirManager();

    inode_t resolve_path(const char* path);
    bool add_entry(inode_t parent, const char* name, inode_t inode);
    bool remove_entry(inode_t parent, const char* name);

private:
    struct DirEntry {
        char     name[FDFS_MAX_NAME_LEN];
        inode_t  inode;
        bool     used;
    };

    struct DirNode {
        inode_t  inode;
        DirEntry entries[FDFS_MAX_DIR_ENTRIES];
    };

    DirNode dir_nodes[FDFS_MAX_INODES];
    bool    inode_used[FDFS_MAX_INODES];

    std::shared_mutex tree_lock;
};