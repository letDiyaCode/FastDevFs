# FastDevFs Algorithms Documentation

This document provides a comprehensive overview of all algorithms used in FastDevFs, including their time and space complexities.

## Table of Contents

1. [Data Structures](#data-structures)
2. [Tree Operations](#tree-operations)
3. [Hash Map Operations](#hash-map-operations)
4. [Persistence Operations](#persistence-operations)
5. [Complexity Summary](#complexity-summary)

---

## Data Structures

### N-ary Tree Structure

The core data structure is an **N-ary tree** implemented using arrays and pointers. Each node contains:

- **parent** (int): Index of parent node
- **firstchild** (int): Index of first child node
- **nextsibling** (int): Index of next sibling node
- **metadata**: Contains inode number and filename (256-byte fixed array)
- **isdeleted** (bool): Deletion flag
- **nextfree** (int): Pointer to next free node in free list

**Structure Layout**:
```
struct treenode {
    int nextfree;           // -1 or index of next free node
    bool isdeleted;         // true if node is in free list
    int firstchild;         // -1 or index of first child
    int nextsibling;        // -1 or index of next sibling  
    int parent;             // -1 or index of parent
    metadate metadata;      // inode and filename
};
```

**Design Rationale**: This sibling-pointer representation allows efficient traversal and manipulation. Each parent maintains a pointer only to its first child, and children are linked via sibling pointers.

### Hash Map Structure

The hash map uses **static allocation** with **linear probing** for collision resolution:

```c
struct hash_entry_t {
    char key[MAX_KEY_LENGTH + 1];  // 256 bytes
    int value;                      // Tree node index
    bool occupied;                  // Slot occupation flag
    uint64_t hash;                  // Cached hash value
};

struct hashmap_t {
    size_t size;                           // Number of entries
    hash_entry_t entries[MAX_HASH_ENTRIES]; // 150,000 entries
};
```

**Capacity**: 150,000 entries (supporting up to 100,000 tree nodes with ~67% load factor)

---

## Tree Operations

### 1. Initialize (`initialize`)

**Algorithm**: Linear initialization of all tree nodes

**Steps**:
1. Iterate through all 100,000 nodes
2. Set each node's `isdeleted = true`
3. Link nodes into free list: `node[i].nextfree = i+1`
4. Set last node's `nextfree = -1`
5. Set `firstfree = 1` (reserve index 0 for root concept)
6. Initialize `nodeallocated = 1`

**Time Complexity**: `O(n)` where n = 100,000
**Space Complexity**: `O(1)` (in-place initialization)
**Invocations**: Called once at startup or when creating new filesystem

---

### 2. Insert (`insert`)

**Algorithm**: Insert a new file/directory node into the tree

**Steps**:
1. **Validation** (O(1)):
   - Check filename is not empty
   - Check treefile state is valid
   - Check if filename already exists in hash map

2. **Allocate Node** (O(1)):
   - Get free node from `firstfree` pointer
   - Update `firstfree` to next free node
   - If no free nodes, perform recovery scan (O(n) worst case, rare)

3. **Hash Map Insert** (O(1) average):
   - Add `filename -> node_index` mapping
   - Linear probing if collision occurs

4. **Parent Linking** (O(1)):
   - Find parent index from hash map
   - Default to root (index 0) if parent doesn't exist
   - Insert as first child of parent
   - Link to existing siblings

5. **Metadata Update** (O(1)):
   - Copy filename to node (using `strncpy`)
   - Set `isdeleted = false`
   - Initialize child pointers

**Time Complexity**: 
- **Average Case**: `O(1)` - All operations are constant time
- **Worst Case**: `O(n)` - Recovery scan if free list is corrupted

**Space Complexity**: `O(1)` - Only local variables used

**Example**:
```
insert("docs", "root", file);
// Creates: root -> docs
```

---

### 3. Delete (`delete1`)

**Algorithm**: Recursively delete a node and all its descendants

**Steps**:
1. **Validation** (O(1)):
   - Check filename exists
   - Prevent root node deletion (critical protection)
   - Check node is not already deleted

2. **Recursive Deletion** (`delete_subtree_recursive`):
   - Traverse all children using firstchild/nextsibling pointers
   - Recursively delete each child subtree
   - Maximum recursion depth limited to prevent stack overflow

3. **Per-Node Deletion** (O(1) per node):
   - Remove from parent's child list
   - Remove from hash map
   - Add to free list
   - Clear metadata
   - Update `nodeallocated` counter

**Time Complexity**:
- **Best Case**: `O(1)` - Deleting leaf node
- **Average Case**: `O(k)` - Where k is number of descendants
- **Worst Case**: `O(n)` - Deleting node with all descendants

**Space Complexity**: 
- `O(d)` where d is maximum tree depth (recursion stack)
- Depth limited to prevent overflow (max 1000 levels)

**Cycle Prevention**: Maximum iteration counters prevent infinite loops from corrupted sibling chains

**Example**:
```
Tree: root -> dir1 -> file1
                  \-> file2
delete1("dir1", file);
// Deletes: dir1, file1, file2 (entire subtree)
```

---

### 4. Change Parent (`change_parent`)

**Algorithm**: Move a node to a different parent (supports filesystem mv/rename)

**Steps**:
1. **Validation** (O(1)):
   - Check node and new parent exist
   - Prevent moving root node
   - Check if already under target parent (no-op)

2. **Cycle Detection** (O(n) worst case):
   - **Upward Check**: Walk parent chain of new parent to ensure it's not a descendant
   - **Downward Check**: Traverse subtree of node being moved
   - Depth limits prevent infinite loops

3. **Unlink from Old Parent** (O(k)):
   - If first child: update parent's firstchild pointer
   - Otherwise: traverse siblings to find and update pointer
   - k = number of siblings

4. **Link to New Parent** (O(1)):
   - Insert as first child of new parent
   - Update parent and sibling pointers

**Time Complexity**:
- **Best Case**: `O(1)` - No cycle, node is first child
- **Average Case**: `O(k)` - k siblings to traverse
- **Worst Case**: `O(n)` - Deep cycle detection traversal

**Space Complexity**: `O(1)` - Only local variables used

**Cycle Prevention**: Critical for tree integrity. Without cycle detection, operations like:
```
move(parent, child)  // Would create: parent -> child -> parent (CYCLE)
```
would corrupt the tree structure.

**Example**:
```
Tree: root -> dir1 -> file1
           \-> dir2
change_parent("file1", "dir2", file);
// Result: root -> dir1
              \-> dir2 -> file1
```

---

### 5. Hash Index Lookup (`hashindex`)

**Algorithm**: Retrieve tree node index for a given filename

**Steps**:
1. **Thread Safety** (O(1)): Acquire recursive mutex lock
2. **Hash Lookup** (O(1) average): Query hash map for filename
3. **Return Index**: Return tree node index or -1 if not found

**Time Complexity**: `O(1)` average, `O(k)` worst case (k = probe length)
**Space Complexity**: `O(1)`

---

## Hash Map Operations

### Hash Function (`poly_hash`)

**Algorithm**: Polynomial rolling hash

**Formula**:
```
hash(s) = s[0] * BASE^(n-1) + s[1] * BASE^(n-2) + ... + s[n-1] * BASE^0
```

Where:
- `BASE = 131` (prime-like constant)
- Computed modulo 2^64 (uint64_t overflow)

**Steps**:
1. Initialize hash = 0
2. For each character: `hash = hash * 131 + char_value`
3. Return final hash value

**Time Complexity**: `O(k)` where k = key length (up to 255 chars)
**Space Complexity**: `O(1)`

**Collision Behavior**: Different strings may hash to same value, resolved by linear probing

---

### Insert (`hashmap_ref`)

**Algorithm**: Insert key-value pair or return reference to existing value

**Steps**:
1. **Validation** (O(1)):
   - Check key length ≤ 255 bytes
   - Compute hash value

2. **Lookup** (O(1) average):
   - Start at index = `hash % MAX_HASH_ENTRIES` (150,000)
   - Linear probe: check each slot sequentially
   - If occupied and key matches: return existing entry
   - If empty: found insertion point

3. **Insert** (O(1)):
   - Copy key to entry
   - Initialize value = 0
   - Set occupied = true
   - Increment size

**Time Complexity**:
- **Average Case**: `O(1)` - Direct hit or short probe
- **Worst Case**: `O(n)` - Probing entire table (when nearly full)

**Space Complexity**: `O(1)` - Updated in place

**Load Factor**: ~67% (100K items in 150K slots) maintains good performance

---

### Lookup (`hashmap_get`, `hashmap_has`)

**Algorithm**: Find entry by key

**Steps**:
1. Compute hash
2. Linear probe from `hash % MAX_HASH_ENTRIES`
3. Stop when: found matching key, found empty slot, or wrapped around

**Time Complexity**: `O(1)` average, `O(n)` worst case
**Space Complexity**: `O(1)`

---

### Remove (`hashmap_remove`)

**Algorithm**: Delete entry and rehash following cluster

**Steps**:
1. **Find Entry** (O(1) average): Linear probe to find key

2. **Mark Deleted** (O(1)):
   - Set occupied = false
   - Decrement size

3. **Rehash Following Entries** (O(k)):
   - Continue probing forward
   - For each occupied slot: remove and reinsert
   - Maintains probe chain integrity
   - Stop at first empty slot or full wraparound

**Time Complexity**: 
- **Average Case**: `O(1)` - Small cluster to rehash
- **Worst Case**: `O(n)` - Large cluster or full table

**Space Complexity**: `O(1)`

**Why Rehashing?**: Without rehashing, deletions would break probe chains, causing subsequent lookups to fail incorrectly.

**Example**:
```
Slots: [A] [B] [empty] (hash collision: A and B both hash to slot 0)
Remove A: Must rehash B to maintain findability
```

---

## Persistence Operations

### Save (`save_treefile`)

**Algorithm**: Persist entire tree structure to disk using mmap

**Steps**:
1. **Open File** (O(1)): Create/truncate file with O_RDWR | O_CREAT | O_TRUNC

2. **Size File** (O(1)): `ftruncate()` to sizeof(treefile_serializable) (~18 GB)

3. **Memory Map** (O(1)): `mmap()` file into address space

4. **Copy Data** (O(n)):
   - Copy header fields (firstfree, start, size, nodeallocated)
   - Copy hashmap_t structure (150K entries)
   - Copy tree array (100K nodes)
   - Total: ~18 GB of data

5. **Sync to Disk** (O(n)): `msync()` with MS_SYNC flag (blocking)

6. **Cleanup** (O(1)): `munmap()` and `close()`

**Time Complexity**: `O(n)` where n = 100,000 nodes
**Space Complexity**: `O(1)` - No additional memory (mmap uses file-backed memory)

**mmap Advantages**:
- Zero-copy I/O
- OS handles page faults and caching
- Atomic writes when combined with msync

---

### Load (`load_treefile`)

**Algorithm**: Restore tree structure from disk

**Steps**:
1. **Validate File** (O(1)):
   - Check file exists (`stat()`)
   - Verify size matches expected

2. **Memory Map** (O(1)): `mmap()` with PROT_READ

3. **Copy Data** (O(n)):
   - Copy header fields
   - Copy hashmap_t structure
   - Copy tree array

4. **Cleanup** (O(1)): `munmap()` and `close()`

**Time Complexity**: `O(n)` where n = 100,000 nodes
**Space Complexity**: `O(1)` - Direct memory copy

**Error Handling**: Returns false if file doesn't exist or size mismatch

---

### Init or Load (`init_or_load_treefile`)

**Algorithm**: Load existing filesystem or initialize new one

**Steps**:
1. **Check Existence** (O(1)): `stat()` to check if file exists
2. **If exists**: Call `load_treefile()` - O(n)
3. **If not exists**: Call `initialize()` then `save_treefile()` - O(n)

**Time Complexity**: `O(n)`
**Space Complexity**: `O(1)`

**Use Case**: Called at daemon startup for automatic recovery

---

## Complexity Summary

### Tree Operations

| Operation | Average Time | Worst Time | Space | Notes |
|-----------|-------------|------------|-------|-------|
| `initialize` | O(n) | O(n) | O(1) | n = 100,000 nodes |
| `insert` | O(1) | O(n) | O(1) | Worst case: free list recovery |
| `delete1` | O(k) | O(n) | O(d) | k = descendants, d = depth |
| `change_parent` | O(k) | O(n) | O(1) | k = siblings, worst: cycle detection |
| `hashindex` | O(1) | O(k) | O(1) | k = probe length |

### Hash Map Operations

| Operation | Average Time | Worst Time | Space | Notes |
|-----------|-------------|------------|-------|-------|
| `poly_hash` | O(k) | O(k) | O(1) | k = key length (max 255) |
| `hashmap_ref` (insert) | O(1) | O(n) | O(1) | n = table size (150K) |
| `hashmap_get` | O(1) | O(n) | O(1) | Linear probing |
| `hashmap_has` | O(1) | O(n) | O(1) | Linear probing |
| `hashmap_remove` | O(1) | O(n) | O(1) | Worst case: rehash large cluster |

### Persistence Operations

| Operation | Average Time | Worst Time | Space | Notes |
|-----------|-------------|------------|-------|-------|
| `save_treefile` | O(n) | O(n) | O(1) | n = 100K nodes (~18 GB copy) |
| `load_treefile` | O(n) | O(n) | O(1) | n = 100K nodes |
| `init_or_load` | O(n) | O(n) | O(1) | Calls initialize + save or load |

### Algorithm Usage Statistics

Based on typical filesystem operations:

| Algorithm | Frequency | Use Cases |
|-----------|-----------|-----------|
| `insert` | Very High | File/directory creation (touch, mkdir) |
| `delete1` | High | File/directory deletion (rm, rmdir) |
| `change_parent` | Medium | File moves (mv, rename) |
| `hashindex` | Very High | All path lookups (ls, cat, cd) |
| `save_treefile` | Low | Periodic checkpoints, graceful shutdown |
| `load_treefile` | Once | Daemon startup |

---

## Performance Characteristics

### Optimal Cases
- **Fast Inserts**: O(1) - Free list provides instant allocation
- **Fast Lookups**: O(1) - Hash map direct access
- **Fast Moves**: O(1) - When no cycles and node is first child

### Bottlenecks
- **Large Deletions**: O(n) - Deleting directory with many descendants
- **Cycle Detection**: O(n) - Deep tree traversal for move validation  
- **Hash Collisions**: O(k) - Long probe chains when table is full
- **Persistence**: O(n) - Writing 18 GB to disk

### Scalability
- **Fixed Capacity**: 100,000 nodes maximum
- **Hash Map Load**: 67% at capacity (good performance)
- **Memory Usage**: ~18 GB per filesystem (constant, not dependent on actual usage)

---

## Thread Safety

All tree operations use **recursive mutex** (`recursive_mutex`) with **RAII lock guards**:

```cpp
lock_guard<recursive_mutex> lock(file1.mtx);
```

**Properties**:
- **Recursive**: Same thread can acquire multiple times
- **Blocking**: Other threads wait for lock release
- **RAII**: Automatic unlock on scope exit (exception-safe)

**Performance Impact**: O(1) overhead per operation for lock acquisition

---

## References

- **Tree Structure**: N-ary tree with sibling pointers ([Knuth TAOCP Vol. 1](https://en.wikipedia.org/wiki/The_Art_of_Computer_Programming))
- **Hash Function**: Polynomial rolling hash ([Rabin-Karp algorithm](https://en.wikipedia.org/wiki/Rabin%E2%80%93Karp_algorithm))
- **Collision Resolution**: Linear probing ([Open addressing](https://en.wikipedia.org/wiki/Open_addressing))
- **Memory Mapping**: mmap(2) system call ([man 2 mmap](https://man7.org/linux/man-pages/man2/mmap.2.html))
