#include "daemon/hash_store.h"
#include <cstring>
#include <cstdio>

HashStore::HashStore() {
    for (int i = 0; i < FDFS_HASH_BUCKETS; i++)
        buckets[i] = -1;

    for (int i = 0; i < FDFS_HASH_CHAINS; i++)
        chain[i].used = false;
}

unsigned long HashStore::compute_hash(const char* data) {
    unsigned long hash = 5381;
    int c;

    while ((c = *data++))
        hash = ((hash << 5) + hash) + c;

    return hash;
}

int HashStore::allocate_node() {
    for (int i = 0; i < FDFS_HASH_CHAINS; i++) {
        if (!chain[i].used) {
            chain[i].used = true;
            chain[i].next = -1;
            return i;
        }
    }
    return -1;
}

bool HashStore::insert(uint64_t key, const char* data) {
    std::lock_guard<std::mutex> lock(hash_lock);

    unsigned long h = compute_hash(data);
    int bucket = h % FDFS_HASH_BUCKETS;

    int node = allocate_node();
    if (node == -1)
        return false;

    chain[node].key = key;
    snprintf(chain[node].hash, FDFS_HASH_LEN, "%lx", h);

    chain[node].next = buckets[bucket];
    buckets[bucket] = node;

    return true;
}

bool HashStore::verify(uint64_t key, const char* data) {
    std::lock_guard<std::mutex> lock(hash_lock);

    unsigned long h = compute_hash(data);
    char expected[FDFS_HASH_LEN];
    snprintf(expected, FDFS_HASH_LEN, "%lx", h);

    int bucket = h % FDFS_HASH_BUCKETS;
    int curr = buckets[bucket];

    while (curr != -1) {
        if (chain[curr].used &&
            chain[curr].key == key &&
            strcmp(chain[curr].hash, expected) == 0)
            return true;

        curr = chain[curr].next;
    }
    return false;
}

bool HashStore::remove(uint64_t key) {
    std::lock_guard<std::mutex> lock(hash_lock);

    for (int b = 0; b < FDFS_HASH_BUCKETS; b++) {
        int curr = buckets[b];
        int prev = -1;

        while (curr != -1) {
            if (chain[curr].used && chain[curr].key == key) {
                if (prev == -1)
                    buckets[b] = chain[curr].next;
                else
                    chain[prev].next = chain[curr].next;

                chain[curr].used = false;
                return true;
            }
            prev = curr;
            curr = chain[curr].next;
        }
    }
    return false;
}
