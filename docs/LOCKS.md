# Read/Write Locks in FastDevFs

This document explains the usage of Read/Write locks within the FastDevFs codebase (or where they *should* be used if implementing finer-grained locking).

## Current State

Currently, the `treefile` structure uses a `recursive_mutex` (`std::recursive_mutex`) to protect the entire data structure. This is a coarse-grained locking approach where every operation (read or write) acquires the same exclusive lock.

## Recommended Read/Write Lock Usage

To improve concurrency, especially for read-heavy workloads (like `ls -R` or repeated `getattr`), we can verify where Read (Shared) locks and Write (Exclusive) locks are appropriate.

### Read Locks (Shared)

Use a **Read Lock** when the operation only *reads* data from the ADT without modifying it. Multiple threads can hold a read lock simultaneously.

**Applicable Functions:**
*   `hashindex`: Looks up a filename in the hash map.
*   `getattr`: Reads metadata (inode, mode, size, etc.) of a file.
*   `access`: Checks permissions.
*   `opendir` / `readdir`: Reads directory contents (if implemented to just read the list).
*   `read`: Reads file content (if content is separate from metadata, or if metadata is not changing).

**Example Scenario:**
*   User runs `ls` (lists files). This requires reading the directory entries. Multiple `ls` commands can run in parallel with read locks.

### Write Locks (Exclusive)

Use a **Write Lock** when the operation *modifies* the ADT (adds/removes nodes, changes metadata). Only one thread can hold a write lock.

**Applicable Functions:**
*   `initialize`: Sets up the initial structure.
*   `insertfolder` / `mkdir`: Adds a new directory node.
*   `insertfile` / `create`: Adds a new file node.
*   `delete1` / `unlink` / `rmdir`: Removes a node and potentially its children.
*   `change_parent` / `rename`: Moves a node.
*   `write`: Modifies file content or size (metadata update).
*   `truncate`: Changes file size.
*   `utimens`: Updates timestamps.

**Example Scenario:**
*   User runs `mkdir new_dir`. This requires a write lock to safely insert the new node and update the parent's link.

## Implementation Note

To switch to Read/Write locks, `std::shared_mutex` (C++17) or `pthread_rwlock_t` (C) should replace `std::recursive_mutex`.

```cpp
// Example C++17
#include <shared_mutex>

struct treefile {
    // ...
    std::shared_mutex rw_lock;
};

// Reader
int hashindex(...) {
    std::shared_lock<std::shared_mutex> lock(file.rw_lock);
    // ... read ...
}

// Writer
void insertfile(...) {
    std::unique_lock<std::shared_mutex> lock(file.rw_lock);
    // ... write ...
}
```
