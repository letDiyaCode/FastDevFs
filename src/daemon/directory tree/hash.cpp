
#include "../../../include/daemon/directory tree/hash.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* ----------------- Implementation details ----------------- */

/* polynomial base for rolling hash */
static const uint64_t POLY_BASE = 131ULL; /* common small base */

/* polynomial rolling hash (mod 2^64 via uint64_t overflow) */
static uint64_t poly_hash(const char *s) {
    const unsigned char *p = (const unsigned char*)s;
    uint64_t h = 0;
    while (*p) {
        h = h * POLY_BASE + (uint64_t)(*p);
        ++p;
    }
    return h;
}

/* Create/destroy */
hashmap_t *hashmap_create(size_t initial_buckets) {
    /* initial_buckets ignored - we use static allocation */
    hashmap_t *m = (hashmap_t*)malloc(sizeof(hashmap_t));
    if (!m) return NULL;
    
    m->size = 0;
    
    /* Initialize all entries as unoccupied */
    for (size_t i = 0; i < MAX_HASH_ENTRIES; i++) {
        m->entries[i].occupied = false;
        m->entries[i].key[0] = '\0';
        m->entries[i].value = 0;
        m->entries[i].hash = 0;
    }
    
    return m;
}

void hashmap_destroy(hashmap_t *m) {
    if (!m) return;
    /* Just free the structure - no dynamic allocations to clean up */
    free(m);
}

/* Linear probing lookup: return pointer to entry or NULL */
static hash_entry_t *find_entry(hashmap_t *m, const char *key, uint64_t h) {
    if (!m || !key) return NULL;
    
    size_t start_idx = (size_t)(h % MAX_HASH_ENTRIES);
    size_t idx = start_idx;
    
    /* Linear probing */
    do {
        if (!m->entries[idx].occupied) {
            /* Found empty slot - key not present */
            return NULL;
        }
        
        if (m->entries[idx].hash == h && strcmp(m->entries[idx].key, key) == 0) {
            /* Found matching entry */
            return &m->entries[idx];
        }
        
        /* Move to next slot (linear probing) */
        idx = (idx + 1) % MAX_HASH_ENTRIES;
        
    } while (idx != start_idx); /* Stop if we've wrapped around */
    
    return NULL; /* Not found */
}

/* Find first available slot for insertion using linear probing */
static hash_entry_t *find_empty_slot(hashmap_t *m, uint64_t h) {
    if (!m) return NULL;
    
    size_t start_idx = (size_t)(h % MAX_HASH_ENTRIES);
    size_t idx = start_idx;
    
    /* Linear probing to find empty slot */
    do {
        if (!m->entries[idx].occupied) {
            return &m->entries[idx];
        }
        
        idx = (idx + 1) % MAX_HASH_ENTRIES;
        
    } while (idx != start_idx);
    
    return NULL; /* Table is full */
}

/* Public API */
int *hashmap_ref(hashmap_t *m, const char *key) {
    if (!m || !key) return NULL;
    
    /* Check key length */
    size_t keylen = strlen(key);
    if (keylen > MAX_KEY_LENGTH) return NULL;
    
    uint64_t h = poly_hash(key);
    hash_entry_t *e = find_entry(m, key, h);
    
    if (e) {
        /* Found existing entry */
        return &e->value;
    }
    
    /* Not found - create new entry */
    if (m->size >= MAX_HASH_ENTRIES) {
        /* Table is full */
        return NULL;
    }
    
    e = find_empty_slot(m, h);
    if (!e) {
        /* No empty slot found (table full) */
        return NULL;
    }
    
    /* Initialize new entry */
    strncpy(e->key, key, MAX_KEY_LENGTH);
    e->key[MAX_KEY_LENGTH] = '\0';
    e->value = 0;
    e->hash = h;
    e->occupied = true;
    m->size++;
    
    return &e->value;
}

void hashmap_set(hashmap_t *m, const char *key, int value) {
    int *p = hashmap_ref(m, key);
    if (p) *p = value;
}

int hashmap_get(hashmap_t *m, const char *key) {
    if (!m || !key) return 0;
    uint64_t h = poly_hash(key);
    hash_entry_t *e = find_entry(m, key, h);
    if (e) return e->value;
    return 0;
}

int hashmap_has(hashmap_t *m, const char *key) {
    if (!m || !key) return 0;
    uint64_t h = poly_hash(key);
    hash_entry_t *e = find_entry(m, key, h);
    return e ? 1 : 0;
}

int hashmap_remove(hashmap_t *m, const char *key) {
    if (!m || !key) return 0;
    
    size_t keylen = strlen(key);
    if (keylen > MAX_KEY_LENGTH) return 0;
    
    uint64_t h = poly_hash(key);
    size_t start_idx = (size_t)(h % MAX_HASH_ENTRIES);
    size_t idx = start_idx;
    
    /* Find the entry */
    do {
        if (!m->entries[idx].occupied) {
            /* Empty slot - key not found */
            return 0;
        }
        
        if (m->entries[idx].hash == h && strcmp(m->entries[idx].key, key) == 0) {
            /* Found it - mark as unoccupied */
            m->entries[idx].occupied = false;
            m->entries[idx].key[0] = '\0';
            m->entries[idx].value = 0;
            m->size--;
            
            /* Rehash following entries to maintain probe chain integrity */
            size_t rehash_idx = (idx + 1) % MAX_HASH_ENTRIES;
            while (m->entries[rehash_idx].occupied) {
                /* Save entry data */
                char temp_key[MAX_KEY_LENGTH + 1];
                strncpy(temp_key, m->entries[rehash_idx].key, MAX_KEY_LENGTH + 1);
                int temp_value = m->entries[rehash_idx].value;
                uint64_t temp_hash = m->entries[rehash_idx].hash;
                
                /* Remove and reinsert */
                m->entries[rehash_idx].occupied = false;
                m->size--;
                
                /* Reinsert */
                hash_entry_t *new_slot = find_empty_slot(m, temp_hash);
                if (new_slot) {
                    strncpy(new_slot->key, temp_key, MAX_KEY_LENGTH);
                    new_slot->key[MAX_KEY_LENGTH] = '\0';
                    new_slot->value = temp_value;
                    new_slot->hash = temp_hash;
                    new_slot->occupied = true;
                    m->size++;
                }
                
                rehash_idx = (rehash_idx + 1) % MAX_HASH_ENTRIES;
                
                /* Prevent infinite loop */
                if (rehash_idx == idx) break;
            }
            
            return 1;
        }
        
        idx = (idx + 1) % MAX_HASH_ENTRIES;
        
    } while (idx != start_idx);
    
    return 0; /* Not found */
}

size_t hashmap_size(hashmap_t *m) {
    return m ? m->size : 0;
}

void hashmap_clear(hashmap_t *m) {
    if (!m) return;
    
    m->size = 0;
    for (size_t i = 0; i < MAX_HASH_ENTRIES; i++) {
        m->entries[i].occupied = false;
        m->entries[i].key[0] = '\0';
        m->entries[i].value = 0;
        m->entries[i].hash = 0;
    }
}
