# FastDevFs - Directory Tree Implementation

## Overview

FastDevFs is a high-performance file system daemon with an efficient in-memory directory tree data structure. It provides thread-safe operations for managing a hierarchical file system structure using an array-based tree implementation with hash map indexing for O(1) filename lookups.

## Project Structure

```
FastDevFs/
├── include/
│   └── daemon/
│       └── directory tree/
│           ├── adt.h          # Abstract Data Type header (tree structures)
│           └── hash.h          # Hash map header
├── src/
│   └── daemon/
│       └── directory tree/
│           ├── adt.cpp         # Tree operations implementation
│           ├── hash.cpp        # Hash map implementation
│           └── hashtest.cpp    # Hash map test program
├── main.cpp                    # FUSE filesystem main entry
├── CMakeLists.txt              # Build configuration
└── README.md                   # This file
```

## Core Data Structures

### 1. **metadate**
Stores file metadata:
- `inode`: Inode number (-1 if not set)
- `name`: Filename (string)

### 2. **treenode**
Represents a node in the directory tree:
- `nextfree`: Pointer to next free node in free list
- `isdeleted`: Flag indicating if node is deleted/free
- `firstchild`: Index of first child node
- `nextsibling`: Index of next sibling node
- `parent`: Index of parent node
- `metadata`: File metadata (inode, name)

### 3. **header**
Tree header containing:
- `firstfree`: Index of first free node
- `start`: Start index (-1 if not used)
- `size`: Total array size (100,000 nodes)
- `nodeallocated`: Number of allocated nodes (index 0 is pre-allocated for root)
- `hash`: HashMap for filename-to-index lookups

### 4. **treefile**
Main tree structure:
- `head`: Tree header
- `arr[100000]`: Array of tree nodes
- `mtx`: Recursive mutex for thread-safe operations

## Key Features

### Thread Safety
- **Recursive Mutex**: All tree operations are protected by `recursive_mutex`
- **Lock Guard**: Automatic lock management using `lock_guard<recursive_mutex>`
- **Nested Locking**: Supports nested function calls (e.g., `hashindex` called from within `delete1`)

### Single Point of Failure Prevention

#### 1. **Input Validation**
- Empty filename checks
- Treefile state validation
- Bounds checking on all array accesses

#### 2. **State Consistency**
- Tree size validation (must be between 0 and 100000)
- Node state verification before operations
- Free list integrity checks

#### 3. **Recovery Mechanisms**
- Automatic free node search if free list is corrupted
- State restoration on hash map operation failures
- Graceful degradation when operations fail

#### 4. **Cycle Prevention**
- Maximum recursion depth limits
- Iteration count limits in loops
- Parent-child relationship validation

#### 5. **Critical Protection**
- Root node (index 0) cannot be deleted or moved
- Bounds checking prevents out-of-bounds array access
- Deleted node state checking prevents operations on freed nodes

## API Functions

### `initialize(treefile &file1)`
**Purpose**: Initialize the tree structure and free list.

**Behavior**:
- Marks all nodes as deleted
- Builds free list: 0 → 1 → 2 → ... → 99999 → -1
- Reserves index 0 for root node
- Sets `firstfree = 1` and `nodeallocated = 1`
- Thread-safe with mutex locking

**Error Handling**:
- Validates tree size before initialization
- Sets default size if invalid (100,000)

### `hashindex(string filename, treefile &file1)`
**Purpose**: Get array index for a filename using hash map lookup.

**Returns**:
- Array index if found
- -1 if not found

**Behavior**:
- Thread-safe hash map lookup
- Returns -1 for non-existent filenames

### `insert(string filename, string parentname, treefile &file1)`
**Purpose**: Insert a new file/directory node into the tree.

**Parameters**:
- `filename`: Name of the file/directory to insert
- `parentname`: Name of parent directory (empty string = root)
- `file1`: Tree structure to operate on

**Behavior**:
- Allocates free node from free list
- Registers filename in hash map
- Links node to parent (defaults to root if parent doesn't exist)
- Updates `nodeallocated` counter

**Error Handling**:
- Validates input (non-empty filename)
- Checks for duplicate filenames
- Validates treefile state
- Checks available space before insertion
- Recovers free node if free list is corrupted
- Restores state on hash map failures

**Single Point of Failure Protection**:
- Space exhaustion check before allocation
- Free list recovery mechanism
- Hash map operation error recovery

### `delete1(string filename, treefile &file1)`
**Purpose**: Delete a file/directory and its entire subtree.

**Behavior**:
- Recursively deletes all children
- Unlinks from parent's child list
- Removes from hash map
- Returns node to free list
- Updates `nodeallocated` counter

**Error Handling**:
- Validates filename and treefile state
- Prevents deletion of root node (critical protection)
- Handles already-deleted nodes (cleanup hash entry)
- Prevents infinite recursion with depth limits

**Single Point of Failure Protection**:
- Root node deletion prevention (critical system protection)
- Recursion depth limits prevent stack overflow
- Iteration limits prevent infinite loops

### `change_parent(string filename, string newparentname, treefile &file1)`
**Purpose**: Move a node from one parent to another.

**Parameters**:
- `filename`: Name of node to move
- `newparentname`: Name of new parent (empty = root)
- `file1`: Tree structure to operate on

**Behavior**:
- Unlinks node from old parent
- Links node to new parent (as first child)
- Updates sibling pointers

**Error Handling**:
- Validates inputs
- Prevents moving root node
- Prevents moving to non-existent parent
- Prevents moving to same parent (no-op)
- Prevents cycles (cannot move node under itself or descendants)

**Single Point of Failure Protection**:
- Cycle detection prevents tree corruption
- Parent validation prevents invalid operations

## Hash Map Implementation

The project uses a custom hash map implementation with:
- **Polynomial Rolling Hash**: O(1) average case lookup
- **Chaining**: Collision resolution using linked lists
- **Dynamic Resizing**: Automatically grows when load factor > 1.0
- **Remove Function**: Supports removing entries from hash map
- **C++ Wrapper**: `HashMap` class provides RAII and operator[] syntax

### Hash Map Operations
- `hashmap_create()`: Create new hash map
- `hashmap_destroy()`: Destroy hash map
- `hashmap_ref()`: Get/insert entry (returns pointer to value)
- `hashmap_set()`: Set value for key
- `hashmap_get()`: Get value for key
- `hashmap_has()`: Check if key exists
- `hashmap_remove()`: Remove key from hash map
- `hashmap_size()`: Get number of entries

## Design Patterns

### 1. **Free List Management**
- Linked list of free nodes for O(1) allocation
- Efficient memory reuse
- Free list integrity checks and recovery

### 2. **Tree Structure**
- Parent-child-sibling links form directory hierarchy
- Root node always at index 0
- Empty parentname defaults to root

### 3. **Hash Map Integration**
- Fast O(1) filename-to-index lookup
- Integrated with tree operations
- Automatic cleanup on node deletion

### 4. **Thread Safety**
- Single mutex protects entire tree structure
- Recursive mutex allows nested calls
- All public functions are thread-safe

## Error Handling Strategy

### Validation Layers
1. **Input Validation**: Check parameters before processing
2. **State Validation**: Verify treefile state is valid
3. **Bounds Checking**: Prevent array out-of-bounds access
4. **Consistency Checking**: Verify node states and relationships

### Recovery Mechanisms
1. **Free List Recovery**: Search for free nodes if list corrupted
2. **State Restoration**: Rollback on operation failures
3. **Graceful Degradation**: Return safely when operations fail

### Critical Protection
1. **Root Node Protection**: Never allow root deletion
2. **Recursion Limits**: Prevent stack overflow
3. **Loop Limits**: Prevent infinite loops in chains

## Memory Layout

```
Index 0: Reserved for root node (always allocated)
Index 1-99999: Available for file/directory nodes
```

Free list: Linked list of deleted nodes for fast reallocation.

## Thread Safety Guarantees

- **All public functions are thread-safe**
- Operations are atomic (protected by mutex)
- No race conditions in tree structure
- Hash map operations are thread-safe when called within locked sections

## Performance Characteristics

- **Insert**: O(1) average case (hash lookup + free node allocation)
- **Delete**: O(n) where n = subtree size (recursive deletion)
- **Lookup**: O(1) average case (hash map lookup)
- **Change Parent**: O(1) average case
- **Space**: O(1) fixed size array (100,000 nodes)

## Usage Example

```cpp
#include "daemon/directory tree/adt.h"

int main() {
    treefile file;
    
    // Initialize tree
    initialize(file);
    
    // Insert files/directories
    insert("root", "", file);              // Root node
    insert("documents", "root", file);      // Under root
    insert("file1.txt", "documents", file); // Under documents
    
    // Lookup index
    int index = hashindex("file1.txt", file);
    
    // Move file
    change_parent("file1.txt", "root", file);
    
    // Delete (recursively deletes subtree)
    delete1("documents", file);
    
    return 0;
}
```

## Build Instructions

```bash
# Compile source files
g++ -std=c++17 -Iinclude -c src/daemon/directory\ tree/adt.cpp -o adt.o
g++ -std=c++17 -Iinclude -c src/daemon/directory\ tree/hash.cpp -o hash.o

# Link
g++ adt.o hash.o -o program
```

Or use CMake:
```bash
mkdir build && cd build
cmake ..
make
```

## Future Enhancements

- [ ] Persistent storage (save/load tree to disk)
- [ ] Transaction support
- [ ] Backup and restore mechanisms
- [ ] Performance monitoring
- [ ] Additional tree operations (list children, get path, etc.)

## License

See LICENSE file for details.
