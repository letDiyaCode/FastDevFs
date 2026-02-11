#include <iostream>
#include "../../../include/daemon/directory tree/adt.h"
#include "../../../include/daemon/directory tree/hash.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
using namespace std;

int hashindex(string filename, treefile &file1){
    lock_guard<recursive_mutex> lock(file1.mtx);
    if (!file1.head.hash.has(filename)) {
        return -1;  // Return -1 if filename not found
    }
    return file1.head.hash[filename];
}
void insertfolder(string foldername, string parentname, treefile &file1){
    lock_guard<recursive_mutex> lock(file1.mtx);

    // Input validation
    if (foldername.empty()) {
        return; // invalid name
    }

    // Validate treefile
    if (file1.head.size <= 0 || file1.head.size > 100000) {
        return;
    }

    // If name already exists and not deleted, do nothing
    if (file1.head.hash.has(foldername)) {
        int existingIndex = file1.head.hash[foldername];
        if (existingIndex >= 0 && existingIndex < file1.head.size &&
            !file1.arr[existingIndex].isdeleted) {
            return; // already exists
        }
    }

    // Check space
    if (file1.head.nodeallocated >= file1.head.size) {
        return; // full
    }

    int free = file1.head.firstfree;
    if (free < 0 || free >= file1.head.size) {
        // try to find a free node
        for (int i = 1; i < file1.head.size; ++i) {
            if (file1.arr[i].isdeleted) {
                free = i;
                break;
            }
        }
        if (free < 0 || free >= file1.head.size) {
            return; // no free space
        }
    }

    // validate free node
    if (free < 0 || free >= file1.head.size || !file1.arr[free].isdeleted) {
        return;
    }

    int nextf = file1.arr[free].nextfree;
    int index = free;
    file1.head.firstfree = nextf;

    // Add to hash map (with recovery on failure)
    try {
        file1.head.hash[foldername] = index;
    } catch (...) {
        // restore firstfree on failure
        file1.head.firstfree = free;
        return;
    }

    // Determine parent index (empty -> root at 0)
    int parentindex = -1;
    if (parentname.empty()) {
        parentindex = 0;
    } else if (file1.head.hash.has(parentname)) {
        parentindex = file1.head.hash[parentname];
        if (parentindex < 0 || parentindex >= file1.head.size ||
            file1.arr[parentindex].isdeleted) {
            parentindex = 0; // treat deleted/invalid parent as root
        }
    } else {
        parentindex = 0; // default to root
    }

    // Link into parent's child list
    if (parentindex >= 0 && parentindex < file1.head.size) {
        int firstson = file1.arr[parentindex].firstchild;
        file1.arr[parentindex].firstchild = index;

        file1.arr[index].nextsibling = firstson;
        file1.arr[index].parent = parentindex;
    } else {
        file1.arr[index].parent = -1;
        file1.arr[index].nextsibling = -1;
    }

    // Set node properties for a directory
    file1.arr[index].isdeleted = false;
    strncpy(file1.arr[index].metadata.name, foldername.c_str(), 255);
    file1.arr[index].metadata.name[255] = '\0';
    file1.arr[index].firstchild = -1;

    // POSIX-like directory metadata
    file1.arr[index].metadata.mode = S_IFDIR | 0755; // directory with rwxr-xr-x
    file1.arr[index].metadata.uid = getuid();
    file1.arr[index].metadata.gid = getgid();
    file1.arr[index].metadata.size = 0;
    time_t now = time(nullptr);
    file1.arr[index].metadata.atime = now;
    file1.arr[index].metadata.mtime = now;
    file1.arr[index].metadata.ctime = now;
    file1.arr[index].metadata.nlink = 2; // '.' and referenced from parent

    // If a valid parent (non-root negative check), increment parent's nlink for new directory
    if (parentindex >= 0 && parentindex < file1.head.size) {
        // Avoid incrementing if parent is deleted (shouldn't be)
        if (!file1.arr[parentindex].isdeleted) {
            // avoid overflow; but typical counters are small
            file1.arr[parentindex].metadata.nlink++;
        }
    }

    // Update allocated counter (0 reserved)
    if (index != 0) {
        file1.head.nodeallocated++;
    }
}


void insertfile(string filename, string parentname, treefile &file1){
    lock_guard<recursive_mutex> lock(file1.mtx);
    
    // Input validation - Single Point of Failure Prevention
    if (filename.empty()) {
        return;  // Invalid filename
    }
    
    // Check for null or invalid treefile
    if (file1.head.size <= 0 || file1.head.size > 100000) {
        return;  // Invalid treefile state
    }
    
    // Check if filename already exists
    if (file1.head.hash.has(filename)) {
        int existingIndex = file1.head.hash[filename];
        if (existingIndex >= 0 && existingIndex < file1.head.size && 
            !file1.arr[existingIndex].isdeleted) {
            return;  // File already exists
        }
    }
    
    // Check available space - Single Point of Failure Prevention
    if (file1.head.nodeallocated >= file1.head.size) {
        return;  // Tree is full
    }
    
    int free = file1.head.firstfree;
    if (free < 0 || free >= file1.head.size) {
        // Recovery: Try to find a free node
        for (int i = 1; i < file1.head.size; i++) {
            if (file1.arr[i].isdeleted) {
                free = i;
                break;
            }
        }
        if (free < 0 || free >= file1.head.size) {
            return;  // No free space available
        }
    }
    
    // Validate free node before using
    if (free < 0 || free >= file1.head.size || !file1.arr[free].isdeleted) {
        return;  // Invalid free node
    }
    
    int nextf = file1.arr[free].nextfree;
    int index = free;
    
    // Update firstfree to point to next free node
    file1.head.firstfree = nextf;
    
    // Add to hash map - with error checking
    try {
        file1.head.hash[filename] = index;
    } catch (...) {
        // Recovery: Restore firstfree
        file1.head.firstfree = free;
        return;  // Hash map operation failed
    }
    
    // Get parent index (empty parentname means root node at index 0)
    int parentindex = -1;
    if (parentname.empty()) {
        // If parentname is empty, parent is root (index 0)
        parentindex = 0;
    } else if (file1.head.hash.has(parentname)) {
        parentindex = file1.head.hash[parentname];
        // Verify parent is not deleted
        if (parentindex >= 0 && parentindex < file1.head.size && 
            file1.arr[parentindex].isdeleted) {
            parentindex = 0;  // Parent is deleted, treat as root
        }
    } else {
        // Parent doesn't exist, default to root
        parentindex = 0;
    }
    
    // Link to parent
    if (parentindex >= 0 && parentindex < file1.head.size) {
        int firstson = file1.arr[parentindex].firstchild;
        file1.arr[parentindex].firstchild = index;  // new kid given
        
        file1.arr[index].nextsibling = firstson;  // sibling sorted
        file1.arr[index].parent = parentindex;
    } else {
        // No parent (shouldn't happen, but set defaults)
        file1.arr[index].parent = -1;
        file1.arr[index].nextsibling = -1;
    }
    
    // Set node properties
    file1.arr[index].isdeleted = false;
    strncpy(file1.arr[index].metadata.name, filename.c_str(), 255);
    file1.arr[index].metadata.name[255] = '\0';
    file1.arr[index].firstchild = -1;
    
    // Initialize POSIX metadata with defaults
    file1.arr[index].metadata.mode = S_IFREG | 0644;  // Regular file with rw-r--r--
    file1.arr[index].metadata.uid = getuid();
    file1.arr[index].metadata.gid = getgid();
    file1.arr[index].metadata.size = 0;
    time_t now = time(nullptr);
    file1.arr[index].metadata.atime = now;
    file1.arr[index].metadata.mtime = now;
    file1.arr[index].metadata.ctime = now;
    file1.arr[index].metadata.nlink = 1;
    
    // Update nodeallocated counter (but don't count index 0)
    if (index != 0) {
        file1.head.nodeallocated++;
    }
}

// Helper function to recursively delete a subtree
static void delete_subtree_recursive(int index, treefile &file1, int max_depth = 1000) {
    // Bounds checking - Single Point of Failure Prevention
    if (index < 0 || index >= file1.head.size) {
        return;  // Invalid index
    }
    
    if (file1.arr[index].isdeleted) {
        return;  // Already deleted
    }
    
    // Prevent infinite loops/stack overflow - Single Point of Failure Prevention
    if (max_depth <= 0) {
        return;  // Max recursion depth reached
    }
    
    // First, recursively delete all children
    int child = file1.arr[index].firstchild;
    int iteration_count = 0;
    while (child != -1 && child < file1.head.size) {
        // Validate child index to prevent cycles and infinite loops
        if (child < 0 || child >= file1.head.size) {
            break;  // Invalid child index
        }
        
        // Prevent infinite loops in sibling chain
        if (++iteration_count > file1.head.size) {
            break;  // Too many iterations, potential cycle
        }
        
        int nextSibling = file1.arr[child].nextsibling;
        delete_subtree_recursive(child, file1, max_depth - 1);
        child = nextSibling;
    }
    
    // Get filename before clearing metadata
    string filename = file1.arr[index].metadata.name;
    
    // Unlink from parent's child list
    int parentIndex = file1.arr[index].parent;
    if (parentIndex >= 0 && parentIndex < file1.head.size) {
        // If this is the first child, update parent's firstchild
        if (file1.arr[parentIndex].firstchild == index) {
            file1.arr[parentIndex].firstchild = file1.arr[index].nextsibling;
        } else {
            // Find the sibling that points to this node
            int sibling = file1.arr[parentIndex].firstchild;
            while (sibling != -1 && sibling < file1.head.size) {
                if (file1.arr[sibling].nextsibling == index) {
                    file1.arr[sibling].nextsibling = file1.arr[index].nextsibling;
                    break;
                }
                sibling = file1.arr[sibling].nextsibling;
            }
        }
    }
    
    // Remove from hash map
    if (!filename.empty()) {
        file1.head.hash.remove(filename);
    }
    
    // Add to free list
    int free = file1.head.firstfree;
    file1.head.firstfree = index;
    file1.arr[index].isdeleted = true;
    file1.arr[index].nextfree = free;
    
    // Clear metadata and tree links
    file1.arr[index].metadata.inode = -1;
    file1.arr[index].metadata.name[0] = '\0';
    file1.arr[index].metadata.mode = 0;
    file1.arr[index].metadata.uid = 0;
    file1.arr[index].metadata.gid = 0;
    file1.arr[index].metadata.size = 0;
    file1.arr[index].metadata.atime = 0;
    file1.arr[index].metadata.mtime = 0;
    file1.arr[index].metadata.ctime = 0;
    file1.arr[index].metadata.nlink = 0;
    file1.arr[index].firstchild = -1;
    file1.arr[index].nextsibling = -1;
    file1.arr[index].parent = -1;
    
    // Update nodeallocated counter (but keep 0th index reserved)
    if (index != 0) {
        file1.head.nodeallocated--;
    }
}

void delete1(string filename, treefile &file1){
    lock_guard<recursive_mutex> lock(file1.mtx);
    
    // Input validation - Single Point of Failure Prevention
    if (filename.empty()) {
        return;  // Invalid filename
    }
    
    // Validate treefile state
    if (file1.head.size <= 0 || file1.head.size > 100000) {
        return;  // Invalid treefile state
    }
    
    if (!file1.head.hash.has(filename)) {
        return;  // File not found
    }
    
    int index = hashindex(filename, file1);
    if (index < 0 || index >= file1.head.size) {
        return;  // Invalid index
    }
    
    // Prevent deletion of root node (index 0) - Critical Protection
    if (index == 0) {
        return;  // Root node cannot be deleted - Single Point of Failure Protection
    }
    
    // Validate node state before deletion
    if (file1.arr[index].isdeleted) {
        // Node already deleted, but hash entry exists - cleanup
        file1.head.hash.remove(filename);
        return;
    }
    
    // Recursively delete the entire subtree
    delete_subtree_recursive(index, file1, file1.head.size);
}

void change_parent(string filename, string newparentname, treefile &file1){
    lock_guard<recursive_mutex> lock(file1.mtx);
    
    // Input validation - Single Point of Failure Prevention
    if (filename.empty()) {
        return;  // Invalid filename
    }
    
    // Validate treefile state
    if (file1.head.size <= 0 || file1.head.size > 100000) {
        return;  // Invalid treefile state
    }
    
    if (!file1.head.hash.has(filename)) {
        return;  // Node not found
    }
    
    int index = hashindex(filename, file1);
    if (index < 0 || index >= file1.head.size || file1.arr[index].isdeleted) {
        return;  // Invalid index
    }
    
    // Cannot change parent of root node
    if (index == 0) {
        return;
    }
    
    // Get new parent index (empty means root at index 0)
    int newparentindex = -1;
    if (newparentname.empty()) {
        newparentindex = 0;  // Root node
    } else if (file1.head.hash.has(newparentname)) {
        newparentindex = file1.head.hash[newparentname];
        // Verify new parent is not deleted
        if (newparentindex < 0 || newparentindex >= file1.head.size || 
            file1.arr[newparentindex].isdeleted) {
            return;  // Invalid parent
        }
    } else {
        return;  // New parent doesn't exist
    }
    
    // Check if moving to the same parent
    if (file1.arr[index].parent == newparentindex) {
        return;  // Already under this parent
    }
    
    // Check if trying to move node under itself or its own descendant
    // Prevent cycles by checking if:
    // 1. New parent is the node itself (impossible, but check anyway)
    // 2. New parent is a descendant of the node being moved (would create cycle)
    if (newparentindex == index) {
        return;  // Cannot move node under itself
    }
    
    // Check if new parent is a descendant of the node being moved
    int check = file1.arr[newparentindex].parent;
    int depth_limit = file1.head.size; // Prevent infinite loops
    while (check != -1 && check < file1.head.size && depth_limit > 0) {
        if (check == index) {
            return;  // Would create a cycle - new parent is a descendant
        }
        check = file1.arr[check].parent;
        depth_limit--;
    }
    
    // Also check if new parent is in the subtree of the node being moved
    // by checking all descendants of the node being moved
    int child = file1.arr[index].firstchild;
    depth_limit = file1.head.size;
    while (child != -1 && child < file1.head.size && depth_limit > 0) {
        if (child == newparentindex) {
            return;  // Would create a cycle - new parent is a descendant
        }
        // Recursively check all descendants
        int descCheck = file1.arr[child].firstchild;
        while (descCheck != -1 && descCheck < file1.head.size && depth_limit > 0) {
            if (descCheck == newparentindex) {
                return;  // Would create a cycle
            }
            descCheck = file1.arr[descCheck].firstchild;
            depth_limit--;
        }
        child = file1.arr[child].nextsibling;
        depth_limit--;
    }
    
    // Unlink from old parent
    int oldparentindex = file1.arr[index].parent;
    if (oldparentindex >= 0 && oldparentindex < file1.head.size) {
        // If this is the first child, update parent's firstchild
        if (file1.arr[oldparentindex].firstchild == index) {
            file1.arr[oldparentindex].firstchild = file1.arr[index].nextsibling;
        } else {
            // Find the sibling that points to this node
            int sibling = file1.arr[oldparentindex].firstchild;
            while (sibling != -1 && sibling < file1.head.size) {
                if (file1.arr[sibling].nextsibling == index) {
                    file1.arr[sibling].nextsibling = file1.arr[index].nextsibling;
                    break;
                }
                sibling = file1.arr[sibling].nextsibling;
            }
        }
    }
    
    // Link to new parent
    int firstson = file1.arr[newparentindex].firstchild;
    file1.arr[newparentindex].firstchild = index;  // new kid given
    
    file1.arr[index].nextsibling = firstson;  // sibling sorted
    file1.arr[index].parent = newparentindex;
}

void initialize(treefile &file1){
    lock_guard<recursive_mutex> lock(file1.mtx);
    
    // Validate size before initialization - Single Point of Failure Prevention
    if (file1.head.size <= 0 || file1.head.size > 100000) {
        // Recovery: Set default size
        file1.head.size = 100000;
    }
    
    int size = file1.head.size;
    for(int i = 0; i < size; i++){
        file1.arr[i].isdeleted = true;
        file1.arr[i].nextfree = i+1;
        file1.arr[i].firstchild = -1;
        file1.arr[i].nextsibling = -1;
        file1.arr[i].parent = -1;
        file1.arr[i].metadata.inode = -1;
        file1.arr[i].metadata.name[0] = '\0';
        file1.arr[i].metadata.mode = 0;
        file1.arr[i].metadata.uid = 0;
        file1.arr[i].metadata.gid = 0;
        file1.arr[i].metadata.size = 0;
        file1.arr[i].metadata.atime = 0;
        file1.arr[i].metadata.mtime = 0;
        file1.arr[i].metadata.ctime = 0;
        file1.arr[i].metadata.nlink = 0;
    } 
    // Last node's nextfree should be -1
    if (size > 0) {
        file1.arr[size - 1].nextfree = -1;
    }
    // 0th index is reserved for root node
    file1.head.firstfree = 1;  // Start free list from index 1
    file1.head.nodeallocated = 1;  // 0th index is pre-allocated for root
    file1.head.start = -1;  // Initialize start pointer
}

// Persistence implementation using mmap

// Helper structure for serialization (without mutex and without HashMap wrapper)
struct header_serializable {
    int firstfree;
    int start;
    int size;
    int nodeallocated;
    hashmap_t hashdata;  // Directly store the hashmap_t structure, not the wrapper
};

struct treefile_serializable {
    header_serializable head;
    treenode arr[100000];
};

bool save_treefile(const char* filepath, treefile &file1) {
    lock_guard<recursive_mutex> lock(file1.mtx);
    
    if (!filepath) return false;
    
    // Open file for writing
    int fd = open(filepath, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        return false;
    }
    
    // Set file size
    size_t file_size = sizeof(treefile_serializable);
    if (ftruncate(fd, file_size) == -1) {
        close(fd);
        return false;
    }
    
    // Map file to memory
    void* mapped = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return false;
    }
    
    // Copy data to mapped memory (excluding mutex)
    treefile_serializable* serialized = (treefile_serializable*)mapped;
    
    // Copy header fields
    serialized->head.firstfree = file1.head.firstfree;
    serialized->head.start = file1.head.start;
    serialized->head.size = file1.head.size;
    serialized->head.nodeallocated = file1.head.nodeallocated;
    
    // Copy the actual hashmap_t structure (not the wrapper)
    if (file1.head.hash.m) {
        memcpy(&serialized->head.hashdata, file1.head.hash.m, sizeof(hashmap_t));
    }
    
    // Copy tree array
    memcpy(serialized->arr, file1.arr, sizeof(file1.arr));
    
    // Sync to disk
    if (msync(mapped, file_size, MS_SYNC) == -1) {
        munmap(mapped, file_size);
        close(fd);
        return false;
    }
    
    // Cleanup
    munmap(mapped, file_size);
    close(fd);
    
    return true;
}

bool load_treefile(const char* filepath, treefile &file1) {
    lock_guard<recursive_mutex> lock(file1.mtx);
    
    if (!filepath) return false;
    
    // Check if file exists
    struct stat st;
    if (stat(filepath, &st) != 0) {
        return false;
    }
    
    // Verify file size
    size_t expected_size = sizeof(treefile_serializable);
    if ((size_t)st.st_size != expected_size) {
        return false;
    }
    
    // Open file for reading
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        return false;
    }
    
    // Map file to memory
    void* mapped = mmap(NULL, expected_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return false;
    }
    
    // Copy data from mapped memory
    treefile_serializable* serialized = (treefile_serializable*)mapped;
    
    // Copy header fields
    file1.head.firstfree = serialized->head.firstfree;
    file1.head.start = serialized->head.start;
    file1.head.size = serialized->head.size;
    file1.head.nodeallocated = serialized->head.nodeallocated;
    
    // Copy the hashmap_t structure back into the HashMap wrapper
    if (file1.head.hash.m) {
        memcpy(file1.head.hash.m, &serialized->head.hashdata, sizeof(hashmap_t));
    }
    
    // Copy tree array
    memcpy(file1.arr, serialized->arr, sizeof(file1.arr));
    
    // Cleanup
    munmap(mapped, expected_size);
    close(fd);
    
    // Note: mutex is already initialized in the treefile structure
    // HashMap wrapper already has allocated m pointer from construction
    
    return true;
}

bool init_or_load_treefile(const char* filepath, treefile &file1) {
    if (!filepath) return false;
    
    // Check if file exists
    struct stat st;
    if (stat(filepath, &st) == 0) {
        // File exists - load it
        return load_treefile(filepath, file1);
    } else {
        // File doesn't exist - initialize and save
        initialize(file1);
        return save_treefile(filepath, file1);
    }
}


