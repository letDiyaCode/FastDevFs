#ifndef FUSE_COMMON_H
#define FUSE_COMMON_H

#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <string>
#include <mutex>
#include <errno.h>
#include <cstring>
#include <vector>
#include <iostream>
#include "../daemon/directory tree/adt.h"

// Helper to get the treefile instance from FUSE context
inline treefile& get_treefile() {
    struct fuse_context* context = fuse_get_context();
    return *static_cast<treefile*>(context->private_data);
}

// Helper to get parent path from a full path
inline std::string get_parent_path(const std::string& path) {
    if (path == "/" || path.empty()) return "";
    
    size_t last_slash = path.find_last_of('/');
    if (last_slash == std::string::npos) return ""; // Should not happen for absolute paths
    if (last_slash == 0) return "/"; // Parent is root
    
    return path.substr(0, last_slash);
}

// Helper to get filename from full path
inline std::string get_filename(const std::string& path) {
    if (path == "/") return "";
    size_t last_slash = path.find_last_of('/');
    if (last_slash == std::string::npos) return path;
    return path.substr(last_slash + 1);
}

// Persistence path — single source of truth
#define FASTDEVFS_PERSIST_PATH "/tmp/fastdevfs.mmap"

// Helper: persist treefile to disk after every mutating operation.
// Uses mmap+msync so only dirty pages are actually written — fast enough.
inline void persist(treefile& file1) {
    save_treefile(FASTDEVFS_PERSIST_PATH, file1);
}

#endif // FUSE_COMMON_H
