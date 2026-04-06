# FastDevFs Concepts Documentation

This document explains the key technical concepts, design decisions, and implementation details of FastDevFs.

## Table of Contents

1. [Memory-Mapped Files (mmap)](#memory-mapped-files-mmap)
2. [Persistence Strategy](#persistence-strategy)
3. [Fixed-Size Arrays vs Dynamic Allocation](#fixed-size-arrays-vs-dynamic-allocation)
4. [Thread Safety and Synchronization](#thread-safety-and-synchronization)
5. [Free List Management](#free-list-management)
6. [Hash Map Internals](#hash-map-internals)
7. [Tree Structure and Sibling Linking](#tree-structure-and-sibling-linking)
8. [Design Tradeoffs](#design-tradeoffs)

---

## Memory-Mapped Files (mmap)

### What is mmap?

**Memory-mapped I/O** is a POSIX system call that maps a file directly into the process's virtual address space. Instead of using traditional `read()` and `write()` system calls, the file appears as a region of memory that can be accessed with regular pointer operations.

### How mmap Works

```
Traditional I/O:                    Memory-Mapped I/O:
┌──────────────┐                   ┌──────────────┐
│ Application  │                   │ Application  │
└──────┬───────┘                   └──────┬───────┘
       │ read()/write()                   │ Direct memory access
       ▼                                  ▼
┌──────────────┐                   ┌──────────────┐
│ Kernel Buffer│                   │ Page Cache   │
└──────┬───────┘                   └──────┬───────┘
       │ Disk I/O                         │ Demand paging
       ▼                                  ▼
┌──────────────┐                   ┌──────────────┐
│     Disk     │                   │     Disk     │
└──────────────┘                   └──────────────┘
```

### mmap in FastDevFs

FastDevFs uses mmap for persistence in `save_treefile()` and `load_treefile()`:

```cpp
// Save operation
int fd = open(filepath, O_RDWR | O_CREAT | O_TRUNC, 0644);
ftruncate(fd, file_size);  // Pre-allocate file space
void* mapped = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

// Direct memory copy - OS handles disk writes
memcpy(mapped, &treefile_data, file_size);

// Force synchronous write to disk
msync(mapped, file_size, MS_SYNC);
munmap(mapped, file_size);
close(fd);
```

### Advantages of mmap

1. **Zero-Copy I/O**: Data is copied directly between file and memory without intermediate kernel buffers
2. **Demand Paging**: OS loads only needed pages from disk (though we copy everything)
3. **Simplified Code**: Treat file as memory - no need for buffering logic
4. **Page Cache Integration**: OS automatically caches frequently accessed pages
5. **Atomic Operations**: With `MS_SYNC`, data is reliably written to disk

### Disadvantages and Tradeoffs

1. **Address Space Consumption**: Uses ~18 GB of virtual address space (not a problem on 64-bit systems)
2. **Memory Pressure**: OS may swap out pages if physical RAM is limited
3. **File Size**: Must know exact size beforehand (we use fixed size)
4. **Error Handling**: SIGBUS signals on I/O errors (we use size validation)

### Why mmap for FastDevFs?

- **Simplicity**: Serialization is just `memcpy()` - no complex marshaling
- **Performance**: Single system call instead of many `write()` calls
- **Atomicity**: `msync(MS_SYNC)` guarantees data is on disk before returning
- **Recovery**: Easy to verify file integrity by checking size

---

## Persistence Strategy

### Overall Approach

FastDevFs uses a **snapshot-based persistence model**:
- Entire filesystem state is periodically saved to a single file
- On startup, the entire state is loaded from the file
- No incremental updates or journaling

### Serialization Format

The persisted file contains a direct memory dump of two structures:

```cpp
struct header_serializable {
    int firstfree;          // Head of free list
    int start;              // Root reference (currently unused)
    int size;               // Maximum nodes (100,000)
    int nodeallocated;      // Count of allocated nodes
    hashmap_t hashdata;     // Entire hash map (150K entries)
};

struct treefile_serializable {
    header_serializable head;
    treenode arr[100000];   // All tree nodes
};
```

**Total Size**: 
- `header_serializable`: ~18 MB (hashmap_t is largest component)
- `treenode arr`: ~18 GB (100K nodes × ~180 KB each)
- **Total**: ~18 GB

### Save Process

```cpp
bool save_treefile(const char* filepath, treefile &file1) {
    lock_guard<recursive_mutex> lock(file1.mtx);  // Thread safety
    
    // Create/truncate file
    int fd = open(filepath, O_RDWR | O_CREAT | O_TRUNC, 0644);
    
    // Allocate exact size
    ftruncate(fd, sizeof(treefile_serializable));
    
    // Memory map
    void* mapped = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
    // Copy header fields (excluding non-POD mutex and HashMap wrapper)
    serialized->head.firstfree = file1.head.firstfree;
    serialized->head.size = file1.head.size;
    // ... copy other header fields
    
    // Copy hash map data (the underlying hashmap_t, not the C++ wrapper)
    memcpy(&serialized->head.hashdata, file1.head.hash.m, sizeof(hashmap_t));
    
    // Copy tree array
    memcpy(serialized->arr, file1.arr, sizeof(file1.arr));
    
    // Synchronous write to disk
    msync(mapped, file_size, MS_SYNC);
    
    // Cleanup
    munmap(mapped, file_size);
    close(fd);
}
```

### Load Process

```cpp
bool load_treefile(const char* filepath, treefile &file1) {
    lock_guard<recursive_mutex> lock(file1.mtx);
    
    // Validate file exists and has correct size
    struct stat st;
    if (stat(filepath, &st) != 0) return false;
    if (st.st_size != sizeof(treefile_serializable)) return false;
    
    // Memory map read-only
    int fd = open(filepath, O_RDONLY);
    void* mapped = mmap(NULL, expected_size, PROT_READ, MAP_PRIVATE, fd, 0);
    
    // Copy data from mapped memory
    file1.head.firstfree = serialized->head.firstfree;
    // ... copy header fields
    
    memcpy(file1.head.hash.m, &serialized->head.hashdata, sizeof(hashmap_t));
    memcpy(file1.arr, serialized->arr, sizeof(file1.arr));
    
    munmap(mapped, expected_size);
    close(fd);
}
```

### Why Not Serialize HashMap Wrapper?

The `HashMap` C++ wrapper contains:
- A destructor that calls `free()`
- A pointer `hashmap_t *m`

**Problem**: If we directly serialize the wrapper:
1. The pointer value is meaningless after restart (points to old address)
2. The destructor would try to `free()` an invalid pointer

**Solution**: Serialize only the underlying `hashmap_t` structure (POD type), which contains the actual data. On load, we copy into the already-constructed HashMap wrapper's allocated `m` pointer.

### Why Not Serialize Mutex?

Mutexes contain OS-specific handles and state that cannot be meaningfully persisted:
- Lock ownership information
- Wait queues
- OS kernel resources

**Solution**: The `treefile` structure's mutex is always constructed fresh. We only serialize the data fields, not the synchronization primitives.

---

## Fixed-Size Arrays vs Dynamic Allocation

### The Design Choice

FastDevFs uses **fixed-size static arrays** throughout:

```cpp
struct treefile {
    header head;
    treenode arr[100000];      // Fixed array, not std::vector or new[]
    recursive_mutex mtx;
};

struct hashmap_t {
    hash_entry_t entries[MAX_HASH_ENTRIES];  // Fixed array, not dynamic
};

struct metadate {
    char name[256];  // Fixed array, not std::string
};
```

### Why Fixed-Size Arrays?

#### 1. **mmap Compatibility**

Dynamic allocations create pointers that are invalid after restart:

```cpp
// BAD: Won't work with mmap persistence
struct treefile_dynamic {
    treenode* arr;  // Pointer to heap memory
};

// After save and load:
// arr still contains old pointer value → SEGFAULT when dereferenced
```

With fixed arrays:
```cpp
// GOOD: Works with mmap
struct treefile_static {
    treenode arr[100000];  // Directly embedded data
};

// After save and load:
// Array data is at same relative offset → Works correctly
```

#### 2. **Pointer Stability**

Fixed arrays maintain **relative offsets**:
- All tree node references are array indices (integers), not pointers
- Indices remain valid after serialization/deserialization
- `arr[5]` is always at offset `5 * sizeof(treenode)` from start of array

#### 3. **Simplicity**

Serialization becomes trivial:
```cpp
// Just copy bytes
memcpy(dest, src, sizeof(treefile_serializable));
```

No need for:
- Pointer remapping
- Object graph traversal
- Reference counting
- Memory allocator state

#### 4. **Performance**

- **Cache Locality**: Contiguous memory improves cache performance
- **No Allocation Overhead**: No malloc/free calls during operations
- **Predictable Performance**: No hidden allocator locks or system calls

### Tradeoffs

#### Disadvantages

1. **Memory Waste**: Always uses ~18 GB even if storing only 10 files
2. **Fixed Capacity**: Cannot exceed 100,000 nodes
3. **Large Executable**: Structure is very large (though not in executable, in data segment)

#### Why Acceptable for FastDevFs

- **Dev Filesystem**: Designed for development environments with ample RAM
- **Known Limits**: 100K files sufficient for most dev projects
- **Simplicity**: Simpler code is more maintainable

---

## Thread Safety and Synchronization

### Concurrency Model

FastDevFs uses **coarse-grained locking** with a **single recursive mutex** protecting the entire tree:

```cpp
struct treefile {
    // ...
    recursive_mutex mtx;  // One mutex for entire structure
};
```

### Why Recursive Mutex?

**Recursive mutex** allows the same thread to acquire the lock multiple times:

```cpp
void delete_subtree_recursive(int index, treefile &file1) {
    // Lock already held by delete1(), but we can lock again
    lock_guard<recursive_mutex> lock(file1.mtx);
    // ...
    delete_subtree_recursive(child, file1);  // Recursion works!
}
```

**Alternative (non-recursive mutex)** would deadlock:
```cpp
void delete1(...) {
    lock_guard<mutex> lock(...);  // Thread A acquires lock
    hashindex(...);               // Tries to acquire same lock → DEADLOCK
}
```

### RAII Lock Guards

All operations use `lock_guard`:

```cpp
void insert(string filename, string parentname, treefile &file1) {
    lock_guard<recursive_mutex> lock(file1.mtx);  // Automatic lock
    
    // ... operation code ...
    
    if (error_condition) {
        return;  // Lock automatically released (RAII)
    }
    
    // Lock automatically released when function returns
}
```

**Benefits**:
- **Exception Safety**: Lock released even if exception thrown
- **No Deadlocks**: Cannot forget to unlock
- **Clear Scope**: Lock lifetime matches scope

### Performance Implications

**Coarse-Grained Locking**:
- ✅ **Simple**: Easy to reason about correctness
- ✅ **Deadlock-Free**: Only one lock, no lock ordering issues
- ❌ **Low Concurrency**: Only one thread can modify tree at a time
- ❌ **Contention**: All operations serialize

**Alternative (Fine-Grained Locking)**:
```cpp
struct treenode {
    mutex node_lock;  // One lock per node
};
```
- ✅ Higher concurrency
- ❌ Complex deadlock prevention
- ❌ Higher memory overhead
- ❌ Harder to reason about correctness

**Why Coarse-Grained for FastDevFs?**
- Simpler implementation
- Adequate performance for dev filesystem
- Easier to verify correctness

---

## Free List Management

### Concept

Instead of allocating/deallocating nodes dynamically, FastDevFs maintains a **free list** of available nodes:

```
Initially:
Free List: 1 → 2 → 3 → ... → 99999 → ∅
firstfree = 1

After insert("file1"):
Free List: 2 → 3 → ... → 99999 → ∅
firstfree = 2
arr[1] = {isdeleted=false, metadata={name="file1"}, ...}

After delete("file1"):
Free List: 1 → 2 → 3 → ... → 99999 → ∅
firstfree = 1
arr[1] = {isdeleted=true, nextfree=2, ...}
```

### Implementation

Each `treenode` has a `nextfree` field that forms a singly-linked list when deleted:

```cpp
struct treenode {
    int nextfree;       // -1 if allocated, or index of next free node
    bool isdeleted;     // true if in free list
    // ... other fields
};
```

### Allocation

```cpp
// Get node from free list
int free = file1.head.firstfree;       // e.g., 5
int nextf = file1.arr[free].nextfree;  // e.g., 8
file1.head.firstfree = nextf;          // Update head: 5 → 8

// Use node
file1.arr[free].isdeleted = false;
// ... initialize node ...
```**Time Complexity**: O(1) - Just pointer manipulation

### Deallocation

```cpp
// Return node to free list
int free = file1.head.firstfree;       // Current head: 8
file1.head.firstfree = index;          // New head: 5
file1.arr[index].nextfree = free;      // Link: 5 → 8
file1.arr[index].isdeleted = true;
```

**Time Complexity**: O(1) - Just pointer manipulation

### Advantages

1. **O(1) Allocation**: No search for free space
2. **O(1) Deallocation**: No freeing overhead
3. **Reuse**: Deleted nodes are recycled
4. **Persistent**: Free list survives across restarts (saved in `firstfree` field)

### No Fragmentation

Unlike heap allocation, free list doesn't fragment:
- All nodes are same size
- No "holes" that are too small to use
- Every free node can satisfy any allocation request

### Recovery Strategy

If free list becomes corrupted (e.g., cycle or invalid index):

```cpp
// Recovery: scan for any deleted node
for (int i = 1; i < file1.head.size; i++) {
    if (file1.arr[i].isdeleted) {
        free = i;
        break;
    }
}
```

This is O(n) but rare - only on corruption. Normal operation is O(1).

---

## Hash Map Internals

### Static Allocation Design

```cpp
#define MAX_HASH_ENTRIES 150000
#define MAX_KEY_LENGTH 255

struct hash_entry_t {
    char key[MAX_KEY_LENGTH + 1];  // Static, not char*
    int value;
    bool occupied;
    uint64_t hash;
};

struct hashmap_t {
    size_t size;
    hash_entry_t entries[MAX_HASH_ENTRIES];  // Static, not dynamic array
};
```

**Size**: `150,000 entries × ~280 bytes = ~42 MB`

### Linear Probing Collision Resolution

When hash collision occurs:

```
Hash(key) = 42

Probe sequence: 42, 43, 44, 45, ..., 149999, 0, 1, 2, ..., 41

Stop when:
- Found matching key (hit)
- Found empty slot (miss)
- Wrapped full circle (table full)
```

### Insertion Example

```cpp
Insert("file1"):
1. hash = poly_hash("file1") = 0xABCD...  // 64-bit hash
2. idx = hash % 150000 = 42
3. Check entries[42]:
   - If empty: insert here
   - If occupied and key="file1": update value
   - If occupied and key≠"file1": try idx=43 (linear probe)
4. Continue until empty slot or match found
```

### Why Cache Hash Value?

Each entry stores the hash:

```cpp
struct hash_entry_t {
    uint64_t hash;  // Cached hash value
    // ...
};
```

**Benefit**: During linear probing, can quickly skip non-matching entries:

```cpp
if (entries[idx].hash == h && strcmp(entries[idx].key, key) == 0) {
    // Match! (1 string comparison)
} else {
    // No match (0 string comparisons - just integer comparison)
}
```

**Speedup**: Avoids expensive string comparisons on hash mismatches

### Deletion and Rehashing

Simple deletion would break probe chains:

```
Initial state:
entries[42] = {key="file1", ...}    // hash("file1") % 150000 = 42
entries[43] = {key="file2", ...}    // hash("file2") % 150000 = 42 (collision!)

After delete("file1") WITHOUT rehashing:
entries[42] = {occupied=false}
entries[43] = {key="file2", ...}

Lookup("file2"):
1. idx = hash("file2") % 150000 = 42
2. entries[42] is empty → NOT FOUND (WRONG!)
```

**Solution**: Rehash all entries in the probe chain after deletion:

```cpp
hashmap_remove(m, "file1"):
1. Find and mark entries[42] as unoccupied
2. Probe forward: entries[43] is occupied
3. Remove and reinsert entries[43]:
   - Remove: occupied=false
   - Reinsert: finds new position (may be different due to changed probe chain)
4. Continue until empty slot
```

Now `lookup("file2")` works correctly because probe chain is maintained.

### Load Factor Management

- **Capacity**: 150,000 entries
- **Maximum Usage**: 100,000 nodes
- **Load Factor**: 100K / 150K = 67%

**Performance**:
- Below 70% load factor: excellent performance (short probe chains)
- Average probe length: ~2-3 entries
- Worst case: O(n) if clustering occurs

---

## Tree Structure and Sibling Linking

### Representation

FastDevFs uses **first-child, next-sibling** pointers:

```
Logical tree:          Physical representation:
    root                root┐
   /    \                   │ firstchild
  A      B                  ▼
 / \                       A ──nextsibling──> B
C   D                      │                  ∅
                          firstchild
                           ▼
                          C ──nextsibling──> D
                          ∅                  ∅
```

### Data Structure

```cpp
struct treenode {
    int parent;        // Index of parent node
    int firstchild;    // Index of first child (-1 if leaf)
    int nextsibling;   // Index of next sibling (-1 if last)
    // ...
};
```

### Why This Representation?

**Advantages**:
1. **Fixed Node Size**: Every node has same number of pointers (3), regardless of number of children
2. **Efficient Sibling Traversal**: Walk siblings with single pointer: `node = arr[node.nextsibling]`
3. **Fast Child Access**: First child directly accessible: `child = arr[node.firstchild]`

**Alternative (Each node stores array of children)**:
```cpp
struct treenode_alternative {
    int parent;
    vector<int> children;  // Variable size!
};
```
- ❌ Variable size breaks fixed-array design
- ❌ Can't use mmap persistence (vector uses dynamic allocation)
- ❌ More memory overhead

### Insertion at Parent

New children are inserted at **head of sibling list**:

```
Before: parent → A → B → C
After insert("D", "parent"): parent → D → A → B → C
```

**Code**:
```cpp
int firstson = arr[parentindex].firstchild;  // firstson = A
arr[parentindex].firstchild = newindex;      // parent → D
arr[newindex].nextsibling = firstson;        // D → A
```

**Time Complexity**: O(1) - Just three pointer updates

**Alternative (Append at end)**:
```cpp
// Find last sibling
int last = arr[parentindex].firstchild;
while (arr[last].nextsibling != -1) {
    last = arr[last].nextsibling;  // O(k) where k = number of siblings
}
arr[last].nextsibling = newindex;
```
- ❌ O(k) instead of O(1)
- ✅ Preserves insertion order

**Choice**: FastDevFs prioritizes O(1) insertion over ordering

### Traversal Patterns

**All children of node**:
```cpp
int child = arr[node].firstchild;
while (child != -1) {
    // Process child
    child = arr[child].nextsibling;
}
```

**All descendants (depth-first)**:
```cpp
void traverse(int node) {
    int child = arr[node].firstchild;
    while (child != -1) {
        traverse(child);  // Recursive descent
        child = arr[child].nextsibling;
    }
}
```

---

## Design Tradeoffs

### Memory vs Flexibility

| Choice | Memory | Flexibility | Justification |
|--------|--------|-------------|---------------|
| Fixed 100K nodes | ❌ 18 GB always | ❌ Hard limit | ✅ Simple, fast, mmap-compatible |
| Dynamic allocation | ✅ Grows as needed | ✅ No limit | ❌ Complex persistence, pointers invalid after load |

**Decision**: Fixed allocation - simplicity outweighs flexibility for dev filesystem

### Concurrency vs Complexity

| Choice | Performance | Complexity | Correctness |
|--------|------------|------------|-------------|
| Single recursive mutex | ❌ Serial operations | ✅ Simple | ✅ Easy to verify |
| Per-node locks | ✅ Parallel operations | ❌ Complex deadlock prevention | ⚠️ Hard to verify |

**Decision**: Single mutex - correctness and maintainability over maximum throughput

### Ordering vs Performance

| Choice | Insert Time | Preserves Order? | Use Case |
|--------|------------|------------------|----------|
| Insert at head | ✅ O(1) | ❌ No | FastDevFs choice |
| Insert at tail | ❌ O(k) | ✅ Yes | Needed for `ls` sorted output |

**Decision**: O(1) insertion - can sort during traversal if needed

### Persistence Model

| Choice | Complexity | Recovery Time | Crash Safety |
|--------|-----------|---------------|--------------|
| Snapshot (current) | ✅ Simple | ⚠️ O(n) | ⚠️ Only last snapshot |
| Journaling | ❌ Complex | ✅ O(log n) | ✅ All committed ops |

**Decision**: Snapshot - adequate for dev filesystem, much simpler

---

## Summary

FastDevFs makes deliberate tradeoffs favoring **simplicity** and **correctness**:

- **mmap**: Efficient persistence with minimal code
- **Fixed arrays**: Simple serialization, no pointer invalidation- **Coarse locking**: Easy to verify, no deadlocks
- **Free list**: O(1) allocation without heap fragmentation
- **Linear probing**: Simple hash collision resolution
- **Sibling pointers**: Fixed-size nodes, efficient traversal

These choices create a **robust, maintainable** filesystem suitable for development environments.
