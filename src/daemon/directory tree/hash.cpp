
#include "hash.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#ifndef strdup
static char *portable_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
#define strdup portable_strdup
#endif

/* ----------------- Implementation details ----------------- */

#define DEFAULT_BUCKETS 64
/* polynomial base for rolling hash */
static const uint64_t POLY_BASE = 131ULL; /* common small base */

/* entry in chain */
typedef struct entry_t {
    char *key;
    int value;
    uint64_t hash;
    struct entry_t *next;
} entry_t;

struct hashmap_t {
    size_t n_buckets;
    size_t size;         /* number of entries */
    entry_t **buckets;   /* array of bucket heads */
};

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

/* allocate buckets array and zero them */
static entry_t **alloc_buckets(size_t n) {
    entry_t **b = (entry_t**)calloc(n, sizeof(entry_t*));
    return b;
}

/* create/destroy */
hashmap_t *hashmap_create(size_t initial_buckets) {
    if (initial_buckets == 0) initial_buckets = DEFAULT_BUCKETS;
    hashmap_t *m = (hashmap_t*)malloc(sizeof(hashmap_t));
    if (!m) return NULL;
    m->n_buckets = initial_buckets;
    m->size = 0;
    m->buckets = alloc_buckets(m->n_buckets);
    if (!m->buckets) { free(m); return NULL; }
    return m;
}

void hashmap_destroy(hashmap_t *m) {
    if (!m) return;
    for (size_t i = 0; i < m->n_buckets; ++i) {
        entry_t *e = m->buckets[i];
        while (e) {
            entry_t *nx = e->next;
            free(e->key);
            free(e);
            e = nx;
        }
    }
    free(m->buckets);
    free(m);
}

/* internal: insert new entry (assumes key duplicated) */
static entry_t *entry_create(const char *key, uint64_t h) {
    entry_t *e = (entry_t*)malloc(sizeof(entry_t));
    if (!e) return NULL;
    e->key = strdup(key);
    e->value = 0;
    e->hash = h;
    e->next = NULL;
    return e;
}

/* rehash (grow capacity) */
static int rehash(hashmap_t *m, size_t new_buckets) {
    entry_t **newbs = alloc_buckets(new_buckets);
    if (!newbs) return 0;
    for (size_t i = 0; i < m->n_buckets; ++i) {
        entry_t *e = m->buckets[i];
        while (e) {
            entry_t *nx = e->next;
            size_t idx = (size_t)(e->hash % new_buckets);
            e->next = newbs[idx];
            newbs[idx] = e;
            e = nx;
        }
    }
    free(m->buckets);
    m->buckets = newbs;
    m->n_buckets = new_buckets;
    return 1;
}

/* ensure capacity: grow when load factor > 1.0 (size > n_buckets) */
static int ensure_capacity(hashmap_t *m) {
    if (m->size <= m->n_buckets) return 1;
    size_t new_b = m->n_buckets * 2;
    if (new_b < 16) new_b = 16;
    return rehash(m, new_b);
}

/* lookup: return pointer to entry or NULL */
static entry_t *find_entry(hashmap_t *m, const char *key, uint64_t h) {
    size_t idx = (size_t)(h % m->n_buckets);
    entry_t *e = m->buckets[idx];
    while (e) {
        if (e->hash == h && strcmp(e->key, key) == 0) return e;
        e = e->next;
    }
    return NULL;
}

/* Public API */
int *hashmap_ref(hashmap_t *m, const char *key) {
    if (!m || !key) return NULL;
    uint64_t h = poly_hash(key);
    entry_t *e = find_entry(m, key, h);
    if (e) return &e->value;
    /* not found: create */
    if (!ensure_capacity(m)) return NULL;
    e = entry_create(key, h);
    if (!e) return NULL;
    size_t idx = (size_t)(h % m->n_buckets);
    e->next = m->buckets[idx];
    m->buckets[idx] = e;
    m->size += 1;
    return &e->value;
}

void hashmap_set(hashmap_t *m, const char *key, int value) {
    int *p = hashmap_ref(m, key);
    if (p) *p = value;
}

int hashmap_get(hashmap_t *m, const char *key) {
    if (!m || !key) return 0;
    uint64_t h = poly_hash(key);
    entry_t *e = find_entry(m, key, h);
    if (e) return e->value;
    return 0;
}

int hashmap_has(hashmap_t *m, const char *key) {
    if (!m || !key) return 0;
    uint64_t h = poly_hash(key);
    entry_t *e = find_entry(m, key, h);
    return e ? 1 : 0;
}

size_t hashmap_size(hashmap_t *m) {
    return m ? m->size : 0;
}
