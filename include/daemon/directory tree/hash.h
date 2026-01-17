#ifndef SIMPLE_POLY_HASH_H
#define SIMPLE_POLY_HASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* Opaque type for the hash table */
typedef struct hashmap_t hashmap_t;

/* Create/destroy */
hashmap_t *hashmap_create(size_t initial_buckets); /* pass 0 to use default (64) */
void       hashmap_destroy(hashmap_t *m);

/* Return pointer to the integer value for key.
   If key does not exist it is inserted with initial value 0.
   The returned pointer remains valid until the entry is removed (which is only on destroy for this API). */
int       *hashmap_ref(hashmap_t *m, const char *key);

/* Convenience helpers */
void       hashmap_set(hashmap_t *m, const char *key, int value); /* sets value (creates if required) */
int        hashmap_get(hashmap_t *m, const char *key);            /* returns value (0 if not present) */
int        hashmap_has(hashmap_t *m, const char *key);            /* returns 1 if present, 0 otherwise */
int        hashmap_remove(hashmap_t *m, const char *key);         /* removes key, returns 1 if removed, 0 if not found */
size_t     hashmap_size(hashmap_t *m);

/* For C++ users: header contains a small wrapper class to allow using map["key"] syntax.
   If you want purely C usage, ignore the C++ wrapper. */

#ifdef __cplusplus
} /* extern "C" */

#include <string>

/* C++ RAII wrapper that exposes operator[] */
struct HashMap {
    hashmap_t *m;
    HashMap(size_t init_buckets = 0) { m = hashmap_create(init_buckets); }
    ~HashMap() { hashmap_destroy(m); }
    /* operator[] returns int& like std::unordered_map */
    int &operator[](const std::string &k) {
        return *hashmap_ref(m, k.c_str());
    }
    /* raw accessors if needed */
    int get(const std::string &k) const { return hashmap_get(m, k.c_str()); }
    void set(const std::string &k, int v) { hashmap_set(m, k.c_str(), v); }
    bool has(const std::string &k) const { return hashmap_has(m, k.c_str()) != 0; }
    bool remove(const std::string &k) { return hashmap_remove(m, k.c_str()) != 0; }
    size_t size() const { return hashmap_size(m); }
};

#endif /* __cplusplus */

#endif /* SIMPLE_POLY_HASH_H */


