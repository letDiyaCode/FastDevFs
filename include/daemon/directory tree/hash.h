#ifndef SIMPLE_POLY_HASH_H
#define SIMPLE_POLY_HASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Maximum number of hash entries - sized for 100k tree nodes with good load factor */
#define MAX_HASH_ENTRIES 150000
#define MAX_KEY_LENGTH 255

/* Hash entry with static key storage */
typedef struct {
    char key[MAX_KEY_LENGTH + 1];  /* Fixed-size key storage (null-terminated) */
    int value;
    bool occupied;                  /* Whether this slot is occupied */
    uint64_t hash;                 /* Cached hash value */
} hash_entry_t;

/* Hash table with static memory */
typedef struct hashmap_t {
    size_t size;                           /* Number of entries */
    hash_entry_t entries[MAX_HASH_ENTRIES]; /* Static array of entries */
} hashmap_t;

/* Create/destroy */
hashmap_t *hashmap_create(size_t initial_buckets); /* initial_buckets ignored, kept for API compatibility */
void       hashmap_destroy(hashmap_t *m);

/* Return pointer to the integer value for key.
   If key does not exist it is inserted with initial value 0.
   Returns NULL if table is full or key is too long. */
int       *hashmap_ref(hashmap_t *m, const char *key);

/* Convenience helpers */
void       hashmap_set(hashmap_t *m, const char *key, int value); /* sets value (creates if required) */
int        hashmap_get(hashmap_t *m, const char *key);            /* returns value (0 if not present) */
int        hashmap_has(hashmap_t *m, const char *key);            /* returns 1 if present, 0 otherwise */
int        hashmap_remove(hashmap_t *m, const char *key);         /* removes key, returns 1 if removed, 0 if not found */
void       hashmap_clear(hashmap_t *m);                           /* clears all entries */
size_t     hashmap_size(hashmap_t *m);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SIMPLE_POLY_HASH_H */
