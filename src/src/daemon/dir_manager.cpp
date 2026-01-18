#include "daemon/dir_manager.h"
#include <cstring>
#include <mutex>

DirManager::DirManager() {
    for (int i = 0; i < FDFS_MAX_INODES; i++) {
        inode_used[i] = false;
    }

    inode_used[0] = true;
    dir_nodes[0].inode = 0;

    for (int i = 0; i < FDFS_MAX_DIR_ENTRIES; i++)
        dir_nodes[0].entries[i].used = false;
}

inode_t DirManager::resolve_path(const char* path) {
    std::shared_lock lock(tree_lock);

    if (strcmp(path, "/") == 0)
        return 0;

    inode_t current = 0;
    const char* p = path;

    char token[FDFS_MAX_NAME_LEN];
    int idx = 0;

    while (*p) {
        if (*p == '/') {
            token[idx] = '\0';
            idx = 0;

            if (token[0] != '\0') {
                bool found = false;
                for (int i = 0; i < FDFS_MAX_DIR_ENTRIES; i++) {
                    auto& e = dir_nodes[current].entries[i];
                    if (e.used && strcmp(e.name, token) == 0) {
                        current = e.inode;
                        found = true;
                        break;
                    }
                }
                if (!found) return static_cast<inode_t>(-1);
            }
        } else {
            token[idx++] = *p;
        }
        p++;
    }

    token[idx] = '\0';
    if (token[0] != '\0') {
        for (int i = 0; i < FDFS_MAX_DIR_ENTRIES; i++) {
            auto& e = dir_nodes[current].entries[i];
            if (e.used && strcmp(e.name, token) == 0)
                return e.inode;
        }
        return static_cast<inode_t>(-1);
    }

    return current;
}

bool DirManager::add_entry(inode_t parent, const char* name, inode_t inode) {
    std::unique_lock lock(tree_lock);

    if (inode >= FDFS_MAX_INODES || inode_used[inode])
        return false;

    for (int i = 0; i < FDFS_MAX_DIR_ENTRIES; i++) {
        if (!dir_nodes[parent].entries[i].used) {
            strcpy(dir_nodes[parent].entries[i].name, name);
            dir_nodes[parent].entries[i].inode = inode;
            dir_nodes[parent].entries[i].used = true;

            inode_used[inode] = true;
            dir_nodes[inode].inode = inode;

            for (int j = 0; j < FDFS_MAX_DIR_ENTRIES; j++)
                dir_nodes[inode].entries[j].used = false;

            return true;
        }
    }
    return false;
}

bool DirManager::remove_entry(inode_t parent, const char* name) {
    std::unique_lock lock(tree_lock);

    for (int i = 0; i < FDFS_MAX_DIR_ENTRIES; i++) {
        auto& e = dir_nodes[parent].entries[i];
        if (e.used && strcmp(e.name, name) == 0) {
            inode_used[e.inode] = false;
            e.used = false;
            return true;
        }
    }
    return false;
}
