#include "daemon/hash.h"
#include <string.h>

void hash_init(HashTable* ht) {
    for (int i = 0; i < HASH_SIZE; i++)
        ht->buckets[i] = -1;

    for (int i = 0; i < MAX_ENTRIES - 1; i++)
        ht->entries[i].next = i + 1;

    ht->entries[MAX_ENTRIES - 1].next = -1;
    ht->free_head = 0;
}

// Polynomial rolling hash on FULL PATH
uint64_t hash_path_poly(const char* path) {
    const uint64_t P = 131;
    uint64_t h = 0;

    for (const unsigned char* p = (const unsigned char*)path; *p; ++p) {
        h = (h * P + *p) % MAX_ENTRIES;
    }
    return h;
}

bool hash_insert(HashTable* ht, uint64_t hash,
                 const char* key, int value) {
    int bucket = hash % HASH_SIZE;

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
