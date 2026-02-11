#pragma once

#include <stdint.h>
#include <stdbool.h>

#define DEDUP_HASH_SIZE 4096        // number of buckets
#define DEDUP_MAX_ENTRIES 100000    // max stored hashes

#define DEDUP_MAGIC 0xD3D3D3D3

// 256-bit SHA hash
typedef struct {
    uint8_t bytes[32];   // SHA-256 = 32 bytes
} SHA256Hash;

// Chain array element
typedef struct {
    uint64_t poly_hash;  // poly hash of SHA256
    uint32_t file_node_index;  // index of file node in Dir_Manager
    int next;            // linked list chaining for collision resolution
} DedupChainEntry;

// Dedup hash table
typedef struct {
    uint32_t magic;
    int buckets[DEDUP_HASH_SIZE];              // bucket array storing first_indices into chain array
    DedupChainEntry chain[DEDUP_MAX_ENTRIES];  // chain array with hash and file node index
    int free_head;                             // free list head for chain array
} DedupTable;

// Global pointer (mmap like your hash table)
extern DedupTable* g_dedup_table;


// API

void dedup_init(DedupTable* dt);

int dedup_lookup(DedupTable* dt, const SHA256Hash* hash);

bool dedup_insert(DedupTable* dt,
                  const SHA256Hash* hash,
                  uint64_t block_id);

bool dedup_remove(DedupTable* dt,
                  const SHA256Hash* hash);

void dedup_inc_ref(DedupTable* dt, int entry_index);

void dedup_dec_ref(DedupTable* dt, int entry_index);
