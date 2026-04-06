/*
 * DedupIndex — mmap-backed deduplication index implementation.
 *
 * Forward map:  open-addressing hash table keyed by (content_hash, is_library).
 * Reverse map:  flat array indexed by tree index (O(1) lookup).
 */

#include "../include/dedup_index.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

// ============================================================
// DedupIndex public API
// ============================================================

DedupIndex::DedupIndex()
    : store_(nullptr), fd_(-1), mapsize_(0) {}

DedupIndex::~DedupIndex() {
    close();
}

bool DedupIndex::init(const char* mmap_path) {
    if (!mmap_path) return false;

    mapsize_ = sizeof(DedupStore);
    bool needs_init = false;

    fd_ = open(mmap_path, O_RDWR);
    if (fd_ == -1) {
        fd_ = open(mmap_path, O_RDWR | O_CREAT, 0644);
        if (fd_ == -1) {
            std::cerr << "[DedupIndex] Failed to create " << mmap_path << std::endl;
            return false;
        }
        if (ftruncate(fd_, mapsize_) == -1) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        needs_init = true;
    } else {
        struct stat st;
        if (fstat(fd_, &st) == -1) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        if ((size_t)st.st_size != mapsize_) {
            if (ftruncate(fd_, mapsize_) == -1) {
                ::close(fd_);
                fd_ = -1;
                return false;
            }
            needs_init = true;
        }
    }

    void* mapped = mmap(nullptr, mapsize_, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd_, 0);
    if (mapped == MAP_FAILED) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    store_ = (DedupStore*)mapped;

    if (needs_init || store_->magic != DEDUP_MAGIC) {
        // Fresh or incompatible — zero out and initialize
        memset(store_, 0, mapsize_);
        store_->magic = DEDUP_MAGIC;
        store_->policy = (int)DedupPolicy::DEDUP_ALL;
        msync(store_, mapsize_, MS_SYNC);
        std::cout << "[DedupIndex] Initialized new dedup store" << std::endl;
    } else {
        std::cout << "[DedupIndex] Loaded existing dedup store" << std::endl;
    }

    return true;
}

void DedupIndex::close() {
    if (store_) {
        msync(store_, mapsize_, MS_SYNC);
        munmap(store_, mapsize_);
        store_ = nullptr;
    }
    if (fd_ != -1) {
        ::close(fd_);
        fd_ = -1;
    }
}

void DedupIndex::sync() {
    if (store_) {
        msync(store_, mapsize_, MS_ASYNC);
    }
}

// ---- Policy ----

void DedupIndex::set_policy(DedupPolicy p) {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (store_) store_->policy = (int)p;
}

DedupPolicy DedupIndex::get_policy() const {
    // No lock needed — single int read is atomic on all platforms we care about
    if (!store_) return DedupPolicy::DEDUP_NONE;
    return (DedupPolicy)store_->policy;
}

// ---- Hash function for probing ----

uint64_t DedupIndex::hash_key(const char* key) {
    // FNV-1a 64-bit
    uint64_t h = 14695981039346656037ULL;
    for (const char* p = key; *p; ++p) {
        h ^= (uint64_t)(unsigned char)*p;
        h *= 1099511628211ULL;
    }
    return h;
}

// ---- Forward map ----

DedupEntry* DedupIndex::lookup(const char* content_hash, bool is_library) {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (!store_ || !content_hash || content_hash[0] == '\0') return nullptr;

    uint64_t h = hash_key(content_hash);
    uint64_t idx = h % MAX_DEDUP_ENTRIES;

    for (int probe = 0; probe < MAX_DEDUP_ENTRIES; probe++) {
        DedupEntry& e = store_->entries[idx];
        if (!e.occupied) return nullptr;  // Empty slot → not found
        if (e.probe_hash == h &&
            e.is_library == is_library &&
            strcmp(e.content_hash, content_hash) == 0) {
            return &e;  // Match!
        }
        idx = (idx + 1) % MAX_DEDUP_ENTRIES;
    }
    return nullptr;  // Table full (should not happen)
}

DedupEntry* DedupIndex::insert(const char* content_hash, int canonical_index,
                                bool is_library) {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (!store_ || !content_hash || content_hash[0] == '\0') return nullptr;

    uint64_t h = hash_key(content_hash);
    uint64_t idx = h % MAX_DEDUP_ENTRIES;

    for (int probe = 0; probe < MAX_DEDUP_ENTRIES; probe++) {
        DedupEntry& e = store_->entries[idx];
        if (!e.occupied) {
            // Found empty slot — insert here
            strncpy(e.content_hash, content_hash, CONTENT_HASH_LEN);
            e.content_hash[CONTENT_HASH_LEN] = '\0';
            e.canonical_index = canonical_index;
            e.refcount = 1;
            e.is_library = is_library;
            e.occupied = true;
            e.probe_hash = h;
            return &e;
        }
        // Check for existing entry with same key (shouldn't happen if caller checks)
        if (e.probe_hash == h &&
            e.is_library == is_library &&
            strcmp(e.content_hash, content_hash) == 0) {
            return &e;  // Already exists
        }
        idx = (idx + 1) % MAX_DEDUP_ENTRIES;
    }
    return nullptr;  // Table full
}

void DedupIndex::increment_ref(DedupEntry* entry) {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (entry && entry->occupied) {
        entry->refcount++;
    }
}

bool DedupIndex::decrement_ref(DedupEntry* entry) {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (!entry || !entry->occupied) return false;

    entry->refcount--;
    if (entry->refcount <= 0) {
        // Remove the entry
        entry->occupied = false;
        entry->content_hash[0] = '\0';
        entry->refcount = 0;
        entry->canonical_index = -1;
        return true;  // Entry removed
    }
    return false;
}

// ---- Reverse map ----

void DedupIndex::set_inode_hash(int tree_index, const char* content_hash,
                                 bool is_library) {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (!store_ || tree_index < 0 || tree_index >= MAX_DEDUP_INODES) return;

    InodeHashMapping& m = store_->inode_map[tree_index];
    strncpy(m.content_hash, content_hash, CONTENT_HASH_LEN);
    m.content_hash[CONTENT_HASH_LEN] = '\0';
    m.occupied = true;
    m.is_library = is_library;
}

std::string DedupIndex::get_inode_hash(int tree_index) const {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (!store_ || tree_index < 0 || tree_index >= MAX_DEDUP_INODES) return "";

    const InodeHashMapping& m = store_->inode_map[tree_index];
    if (!m.occupied) return "";
    return std::string(m.content_hash);
}

bool DedupIndex::get_inode_is_library(int tree_index) const {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (!store_ || tree_index < 0 || tree_index >= MAX_DEDUP_INODES) return false;
    return store_->inode_map[tree_index].is_library;
}

bool DedupIndex::has_inode(int tree_index) const {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (!store_ || tree_index < 0 || tree_index >= MAX_DEDUP_INODES) return false;
    return store_->inode_map[tree_index].occupied;
}

void DedupIndex::remove_inode(int tree_index) {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (!store_ || tree_index < 0 || tree_index >= MAX_DEDUP_INODES) return;

    InodeHashMapping& m = store_->inode_map[tree_index];
    m.occupied = false;
    m.content_hash[0] = '\0';
    m.is_library = false;
}

// ---- Composite queries ----

bool DedupIndex::is_shared(int tree_index) {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (!store_ || tree_index < 0 || tree_index >= MAX_DEDUP_INODES) return false;

    const InodeHashMapping& m = store_->inode_map[tree_index];
    if (!m.occupied) return false;

    // Look up the forward entry (without locking again — we hold it)
    uint64_t h = hash_key(m.content_hash);
    uint64_t idx = h % MAX_DEDUP_ENTRIES;

    for (int probe = 0; probe < MAX_DEDUP_ENTRIES; probe++) {
        const DedupEntry& e = store_->entries[idx];
        if (!e.occupied) return false;
        if (e.probe_hash == h &&
            e.is_library == m.is_library &&
            strcmp(e.content_hash, m.content_hash) == 0) {
            return e.refcount > 1;
        }
        idx = (idx + 1) % MAX_DEDUP_ENTRIES;
    }
    return false;
}

int DedupIndex::find_other_inode_with_hash(const char* content_hash,
                                            int skip_index) {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (!store_ || !content_hash) return -1;

    for (int i = 0; i < MAX_DEDUP_INODES; i++) {
        if (i == skip_index) continue;
        const InodeHashMapping& m = store_->inode_map[i];
        if (m.occupied && strcmp(m.content_hash, content_hash) == 0) {
            return i;
        }
    }
    return -1;
}

// ---- Locking ----

void DedupIndex::lock() {
    mutex_.lock();
}

void DedupIndex::unlock() {
    mutex_.unlock();
}
