#ifndef DEDUP_INDEX_H
#define DEDUP_INDEX_H

/*
 * DedupIndex — mmap-backed deduplication index.
 *
 * Provides persistent (across restarts) storage for:
 *   1. Forward map:  content_hash → DedupEntry  (hash table, open addressing)
 *   2. Reverse map:  tree_index   → content_hash (direct array, O(1))
 *
 * Thread-safe: all public methods acquire an internal mutex.
 */

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <string>

// ============================================================
// Layout constants
// ============================================================

#define DEDUP_MAGIC          0xDED0F11Eu
#define CONTENT_HASH_LEN     64            // SHA-256 hex string length
#define MAX_DEDUP_ENTRIES     150000        // Forward map capacity
#define MAX_DEDUP_INODES      100000        // Matches TREEFILE_MAX_NODES

// ============================================================
// Dedup policy (runtime-changeable)
// ============================================================

enum class DedupPolicy : int {
    DEDUP_ALL          = 0,   // Dedup both user files and libraries
    DEDUP_USER_ONLY    = 1,   // Dedup user files, skip libraries
    DEDUP_LIBRARY_ONLY = 2,   // Dedup libraries, skip user files
    DEDUP_NONE         = 3    // Disable dedup entirely
};

// ============================================================
// POD structures (mmap-safe, no std:: types)
// ============================================================

// Forward map entry: content_hash → storage info
struct DedupEntry {
    char     content_hash[CONTENT_HASH_LEN + 1]; // Key: SHA-256 hex + NUL
    int      canonical_index;                      // Tree array index of canonical copy
    int      refcount;                             // # of inodes sharing this content
    bool     is_library;                           // Category tag (prevents cross-linking)
    bool     occupied;                             // Hash table slot state
    uint64_t probe_hash;                           // Cached hash-of-key for probing
};

// Reverse map entry: tree_index → current content hash
struct InodeHashMapping {
    char content_hash[CONTENT_HASH_LEN + 1];     // Current content hash for this inode
    bool occupied;                                 // Whether this inode has dedup state
    bool is_library;                               // Cached category of the inode
};

// Top-level mmap'd structure
struct DedupStore {
    uint32_t         magic;
    int              policy;                              // DedupPolicy as int
    DedupEntry       entries[MAX_DEDUP_ENTRIES];           // Hash table
    InodeHashMapping inode_map[MAX_DEDUP_INODES];          // Direct array by tree index
};

// ============================================================
// DedupIndex wrapper class
// ============================================================

class DedupIndex {
public:
    DedupIndex();
    ~DedupIndex();

    // Initialize mmap-backed store. Returns false on failure.
    bool init(const char* mmap_path);

    // Cleanup (msync + munmap).
    void close();

    // ---- Policy ----
    void        set_policy(DedupPolicy p);
    DedupPolicy get_policy() const;

    // ---- Forward map operations ----

    // Lookup an entry by (hash, is_library). Returns nullptr if not found.
    DedupEntry* lookup(const char* content_hash, bool is_library);

    // Insert a new entry (unique content). Returns pointer, or nullptr if full.
    DedupEntry* insert(const char* content_hash, int canonical_index,
                       bool is_library);

    // Increment refcount for existing entry.
    void increment_ref(DedupEntry* entry);

    // Decrement refcount. Returns true if refcount reached 0 (entry removed).
    bool decrement_ref(DedupEntry* entry);

    // ---- Reverse map operations ----

    // Set the hash mapping for a tree index.
    void set_inode_hash(int tree_index, const char* content_hash, bool is_library);

    // Get the content hash for a tree index. Returns empty string if not mapped.
    std::string get_inode_hash(int tree_index) const;

    // Get the is_library flag for a tree index.
    bool get_inode_is_library(int tree_index) const;

    // Check if an inode has dedup state.
    bool has_inode(int tree_index) const;

    // Remove an inode from the reverse map.
    void remove_inode(int tree_index);

    // ---- Composite queries ----

    // Check if a file is currently sharing data (refcount > 1).
    bool is_shared(int tree_index);

    // Find another tree index that shares the same content hash.
    // Returns -1 if none found. Skips skip_index.
    int find_other_inode_with_hash(const char* content_hash, int skip_index);

    // ---- Locking (for external multi-step operations) ----
    void lock();
    void unlock();

    // Sync mmap to disk.
    void sync();

private:
    DedupStore*    store_;
    int            fd_;
    size_t         mapsize_;
    mutable std::recursive_mutex mutex_;

    // Hash a string key for hash-table probing.
    static uint64_t hash_key(const char* key);
};

#endif /* DEDUP_INDEX_H */
