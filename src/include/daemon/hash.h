#pragma once

#include <stdint.h>
#include <stdbool.h>

#define HASH_SIZE    4096
#define MAX_ENTRIES  10000
#define KEY_SIZE     1024   // full path key

// single hash table entry (chained)
typedef struct HashEntry {
    uint64_t hash;              // hash of key
    char     key[KEY_SIZE];     // full key (path)
    int      value;             // node index
    int      next;              // next entry in chain
} HashEntry;

// static hash table
typedef struct HashTable {
    int buckets[HASH_SIZE];
    HashEntry entries[MAX_ENTRIES];
    int free_head;
} HashTable;

// initialization
void hash_init(HashTable* ht);

// full-path polynomial hash
uint64_t hash_path_poly(const char* path);

// operations (key-aware)
bool hash_insert(HashTable* ht, uint64_t hash, const char* key, int value);
int  hash_lookup(HashTable* ht, uint64_t hash, const char* key);
bool hash_remove(HashTable* ht, uint64_t hash, const char* key);
