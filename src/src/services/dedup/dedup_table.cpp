#include "services/dedup/dedup_table.h"
#include <string.h>

DedupTable* g_dedup_table = nullptr;

/* ---------------- POLYNOMIAL HASH ---------------- */

static uint64_t poly_hash_sha256(const SHA256Hash* hash)
{
    const uint64_t P = 131;
    uint64_t h = 0;

    for (int i = 0; i < 32; i++)
        h = (h * P + hash->bytes[i]) % DEDUP_MAX_ENTRIES;

    return h;
}

/* ---------------- INIT ---------------- */

void dedup_init(DedupTable* dt)
{
    dt->magic = DEDUP_MAGIC;

    // Initialize buckets
    for (int i = 0; i < DEDUP_HASH_SIZE; i++)
        dt->buckets[i] = -1;

    // Initialize free list
    for (int i = 0; i < DEDUP_MAX_ENTRIES - 1; i++)
    {
        dt->chain[i].next = i + 1;
        dt->chain[i].ref_count = 0;
    }

    dt->chain[DEDUP_MAX_ENTRIES - 1].next = -1;
    dt->chain[DEDUP_MAX_ENTRIES - 1].ref_count = 0;

    dt->free_head = 0;
}

/* ---------------- LOOKUP ---------------- */

int dedup_lookup(DedupTable* dt, const SHA256Hash* hash)
{
    uint64_t h = poly_hash_sha256(hash);
    int bucket = h % DEDUP_HASH_SIZE;

    for (int i = dt->buckets[bucket]; i != -1; i = dt->chain[i].next)
    {
        if (dt->chain[i].poly_hash == h)
        {
            // NOTE:
            // Currently matching only poly_hash.
            // If you want collision safety, compare full SHA256 here.
            return i;
        }
    }

    return -1;
}

/* ---------------- INSERT ---------------- */

bool dedup_insert(DedupTable* dt,
                  const SHA256Hash* hash,
                  uint64_t file_node_index)
{
    uint64_t h = poly_hash_sha256(hash);
    int bucket = h % DEDUP_HASH_SIZE;

    // Check if already exists
    for (int i = dt->buckets[bucket]; i != -1; i = dt->chain[i].next)
    {
        if (dt->chain[i].poly_hash == h)
            return false;
    }

    // No space left
    if (dt->free_head == -1)
        return false;

    int idx = dt->free_head;
    dt->free_head = dt->chain[idx].next;

    dt->chain[idx].poly_hash = h;
    dt->chain[idx].file_node_index = file_node_index;
    dt->chain[idx].ref_count = 1;

    // Insert at bucket head
    dt->chain[idx].next = dt->buckets[bucket];
    dt->buckets[bucket] = idx;

    return true;
}

/* ---------------- REMOVE ---------------- */

bool dedup_remove(DedupTable* dt,
                  const SHA256Hash* hash)
{
    uint64_t h = poly_hash_sha256(hash);
    int bucket = h % DEDUP_HASH_SIZE;

    int prev = -1;

    for (int i = dt->buckets[bucket]; i != -1; prev = i, i = dt->chain[i].next)
    {
        if (dt->chain[i].poly_hash == h)
        {
            // Only remove if ref_count == 0
            if (dt->chain[i].ref_count > 0)
                return false;

            if (prev == -1)
                dt->buckets[bucket] = dt->chain[i].next;
            else
                dt->chain[prev].next = dt->chain[i].next;

            // Return to free list
            dt->chain[i].next = dt->free_head;
            dt->free_head = i;

            return true;
        }
    }

    return false;
}

/* ---------------- INC REF ---------------- */

void dedup_inc_ref(DedupTable* dt, int entry_index)
{
    if (entry_index < 0 || entry_index >= DEDUP_MAX_ENTRIES)
        return;

    dt->chain[entry_index].ref_count++;
}

/* ---------------- DEC REF ---------------- */

void dedup_dec_ref(DedupTable* dt, int entry_index)
{
    if (entry_index < 0 || entry_index >= DEDUP_MAX_ENTRIES)
        return;

    if (dt->chain[entry_index].ref_count == 0)
        return;

    dt->chain[entry_index].ref_count--;

    // If refcount becomes zero → remove entry
    if (dt->chain[entry_index].ref_count == 0)
    {
        uint64_t h = dt->chain[entry_index].poly_hash;
        int bucket = h % DEDUP_HASH_SIZE;

        int prev = -1;

        for (int i = dt->buckets[bucket]; i != -1; prev = i, i = dt->chain[i].next)
        {
            if (i == entry_index)
            {
                if (prev == -1)
                    dt->buckets[bucket] = dt->chain[i].next;
                else
                    dt->chain[prev].next = dt->chain[i].next;

                dt->chain[i].next = dt->free_head;
                dt->free_head = i;
                break;
            }
        }
    }
}