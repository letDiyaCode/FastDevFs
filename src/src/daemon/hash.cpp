#include "daemon/hash.h"
#include <string.h>

static const uint64_t HASH_SEPARATOR = 1315423911ULL;

void hash_init(HashTable* ht) {
    for (int i = 0; i < HASH_SIZE; i++)
        ht->buckets[i] = -1;

    for (int i = 0; i < MAX_ENTRIES - 1; i++)
        ht->entries[i].next = i + 1;

    ht->entries[MAX_ENTRIES - 1].next = -1;
    ht->free_head = 0;
}

uint64_t hash_combine(uint64_t parent_hash, const char* name) {
    uint64_t h = parent_hash;

    for (const unsigned char* p = (const unsigned char*)name; *p; ++p)
        h ^= (h << 5) + (h >> 2) + *p;

    return h ^ HASH_SEPARATOR;
}

bool hash_insert(HashTable* ht, uint64_t hash,
                 const char* key, int value) {
    int bucket = hash % HASH_SIZE;

    // reject exact duplicate key
    for (int i = ht->buckets[bucket]; i != -1; i = ht->entries[i].next) {
        if (ht->entries[i].hash == hash &&
            strcmp(ht->entries[i].key, key) == 0)
            return false;
    }

    if (ht->free_head == -1)
        return false;

    int idx = ht->free_head;
    ht->free_head = ht->entries[idx].next;

    ht->entries[idx].hash = hash;
    strncpy(ht->entries[idx].key, key, KEY_SIZE - 1);
    ht->entries[idx].key[KEY_SIZE - 1] = '\0';
    ht->entries[idx].value = value;

    ht->entries[idx].next = ht->buckets[bucket];
    ht->buckets[bucket] = idx;

    return true;
}

int hash_lookup(HashTable* ht, uint64_t hash,
                const char* key) {
    int bucket = hash % HASH_SIZE;

    for (int i = ht->buckets[bucket]; i != -1; i = ht->entries[i].next) {
        if (ht->entries[i].hash == hash &&
            strcmp(ht->entries[i].key, key) == 0)
            return ht->entries[i].value;
    }
    return -1;
}

bool hash_remove(HashTable* ht, uint64_t hash,
                 const char* key) {
    int bucket = hash % HASH_SIZE;
    int prev = -1;

    for (int i = ht->buckets[bucket]; i != -1; prev = i, i = ht->entries[i].next) {
        if (ht->entries[i].hash == hash &&
            strcmp(ht->entries[i].key, key) == 0) {

            if (prev == -1)
                ht->buckets[bucket] = ht->entries[i].next;
            else
                ht->entries[prev].next = ht->entries[i].next;

            ht->entries[i].next = ht->free_head;
            ht->free_head = i;
            return true;
        }
    }
    return false;
}
