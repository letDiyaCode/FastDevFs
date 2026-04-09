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
#define MAX_KEY_LENGTH 299

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
hashmap_t *hashmap_create(size_t initial_buckets);
void       hashmap_destroy(hashmap_t *m);

/* Return pointer to the integer value for key */
int       *hashmap_ref(hashmap_t *m, const char *key);

/* Convenience helpers */
void       hashmap_set(hashmap_t *m, const char *key, int value);
int        hashmap_get(hashmap_t *m, const char *key);
int        hashmap_has(hashmap_t *m, const char *key);
int        hashmap_remove(hashmap_t *m, const char *key);
void       hashmap_clear(hashmap_t *m);
size_t     hashmap_size(hashmap_t *m);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SIMPLE_POLY_HASH_H */
