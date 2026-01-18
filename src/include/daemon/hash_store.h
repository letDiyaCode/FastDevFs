#pragma once

#include <cstdint>
#include <mutex>

constexpr int FDFS_HASH_BUCKETS = 128;
constexpr int FDFS_HASH_CHAINS  = 1024;
constexpr int FDFS_HASH_LEN     = 64;

/**
 * Hash Store ADT
 * Static hash table with bucket + chain arrays
 * Thread-safe
 */
class HashStore {
public:
    HashStore();

    bool insert(uint64_t key, const char* data);
    bool verify(uint64_t key, const char* data);
    bool remove(uint64_t key);

private:
    struct ChainNode {
        uint64_t key;
        char     hash[FDFS_HASH_LEN];
        int      next;
        bool     used;
    };

    int       buckets[FDFS_HASH_BUCKETS];   // index into chain[]
    ChainNode chain[FDFS_HASH_CHAINS];

    std::mutex hash_lock;

    unsigned long compute_hash(const char* data);
    int allocate_node();
};
