#include <iostream>
#include "../../../include/daemon/directory tree/adt.h"
#include "../../../include/daemon/directory tree/hash.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
using namespace std;

// Global mutex definition
recursive_mutex treefile_mtx;

int hashindex(string filename, treefile &file1){
    lock_guard<recursive_mutex> lock(treefile_mtx);
    if (!hashmap_has(&file1.hashdata, filename.c_str())) {
        return -1;
    }
    return hashmap_get(&file1.hashdata, filename.c_str());
}

void insertfolder(string foldername, string parentname, treefile &file1){
    lock_guard<recursive_mutex> lock(treefile_mtx);

    if (foldername.empty()) return;
    if (file1.size <= 0 || file1.size > TREEFILE_MAX_NODES) return;

    // If name already exists and not deleted, do nothing
    if (hashmap_has(&file1.hashdata, foldername.c_str())) {
        int existingIndex = hashmap_get(&file1.hashdata, foldername.c_str());
        if (existingIndex >= 0 && existingIndex < file1.size &&
            !file1.arr[existingIndex].isdeleted) {
            return;
        }
    }

    // Allocate: free list first (priority), then bump allocator
    int index = -1;
    if (file1.firstfree != -1 && file1.firstfree >= 0 && file1.firstfree < file1.size) {
        index = file1.firstfree;
        file1.firstfree = file1.arr[index].nextfree;
    } else if (file1.nodeallocated < file1.size) {
        index = file1.nodeallocated;
        file1.nodeallocated++;
    } else {
        return; // full
    }

    // Add to hash map
    hashmap_set(&file1.hashdata, foldername.c_str(), index);

    // Determine parent index (empty -> root at 0)
    int parentindex = 0;
    if (!parentname.empty() && hashmap_has(&file1.hashdata, parentname.c_str())) {
        parentindex = hashmap_get(&file1.hashdata, parentname.c_str());
        if (parentindex < 0 || parentindex >= file1.size ||
            file1.arr[parentindex].isdeleted) {
            parentindex = 0;
        }
    }

    // Link into parent's child list (prepend) with prevsibling maintenance
    if (parentindex >= 0 && parentindex < file1.size) {
        int firstson = file1.arr[parentindex].firstchild;
        file1.arr[parentindex].firstchild = index;

        file1.arr[index].nextsibling = firstson;
        file1.arr[index].prevsibling = -1;
        if (firstson >= 0 && firstson < file1.size) {
            file1.arr[firstson].prevsibling = index;
        }
        file1.arr[index].parent = parentindex;
    } else {
        file1.arr[index].parent = -1;
        file1.arr[index].nextsibling = -1;
        file1.arr[index].prevsibling = -1;
    }

    // Set node properties for a directory
    file1.arr[index].isdeleted = false;
    file1.arr[index].nextfree = -1;
    strncpy(file1.arr[index].metadata.name, foldername.c_str(), 255);
    file1.arr[index].metadata.name[255] = '\0';
    file1.arr[index].firstchild = -1;

    file1.arr[index].metadata.mode = S_IFDIR | 0755;
    file1.arr[index].metadata.uid = getuid();
    file1.arr[index].metadata.gid = getgid();
    file1.arr[index].metadata.size = 0;
    time_t now = time(nullptr);
    file1.arr[index].metadata.atime = now;
    file1.arr[index].metadata.mtime = now;
    file1.arr[index].metadata.ctime = now;
    file1.arr[index].metadata.nlink = 2; // '.' and parent reference

    // Increment parent's nlink for new directory
    if (parentindex >= 0 && parentindex < file1.size &&
        !file1.arr[parentindex].isdeleted) {
        file1.arr[parentindex].metadata.nlink++;
    }
}

void insertfile(string filename, string parentname, treefile &file1){
    lock_guard<recursive_mutex> lock(treefile_mtx);

    if (filename.empty()) return;
    if (file1.size <= 0 || file1.size > TREEFILE_MAX_NODES) return;

    // Check if filename already exists
    if (hashmap_has(&file1.hashdata, filename.c_str())) {
        int existingIndex = hashmap_get(&file1.hashdata, filename.c_str());
        if (existingIndex >= 0 && existingIndex < file1.size &&
            !file1.arr[existingIndex].isdeleted) {
            return;
        }
    }

    // Allocate: free list first (priority), then bump allocator
    int index = -1;
    if (file1.firstfree != -1 && file1.firstfree >= 0 && file1.firstfree < file1.size) {
        index = file1.firstfree;
        file1.firstfree = file1.arr[index].nextfree;
    } else if (file1.nodeallocated < file1.size) {
        index = file1.nodeallocated;
        file1.nodeallocated++;
    } else {
        return; // full
    }

    // Add to hash map
    hashmap_set(&file1.hashdata, filename.c_str(), index);

    // Get parent index (empty parentname means root node at index 0)
    int parentindex = 0;
    if (!parentname.empty() && hashmap_has(&file1.hashdata, parentname.c_str())) {
        parentindex = hashmap_get(&file1.hashdata, parentname.c_str());
        if (parentindex < 0 || parentindex >= file1.size ||
            file1.arr[parentindex].isdeleted) {
            parentindex = 0;
        }
    }

    // Link into parent's child list (prepend) with prevsibling maintenance
    if (parentindex >= 0 && parentindex < file1.size) {
        int firstson = file1.arr[parentindex].firstchild;
        file1.arr[parentindex].firstchild = index;

        file1.arr[index].nextsibling = firstson;
        file1.arr[index].prevsibling = -1;
        if (firstson >= 0 && firstson < file1.size) {
            file1.arr[firstson].prevsibling = index;
        }
        file1.arr[index].parent = parentindex;
    } else {
        file1.arr[index].parent = -1;
        file1.arr[index].nextsibling = -1;
        file1.arr[index].prevsibling = -1;
    }

    // Set node properties for a file
    file1.arr[index].isdeleted = false;
    file1.arr[index].nextfree = -1;
    strncpy(file1.arr[index].metadata.name, filename.c_str(), 255);
    file1.arr[index].metadata.name[255] = '\0';
    file1.arr[index].firstchild = -1;

    file1.arr[index].metadata.mode = S_IFREG | 0644;
    file1.arr[index].metadata.uid = getuid();
    file1.arr[index].metadata.gid = getgid();
    file1.arr[index].metadata.size = 0;
    time_t now = time(nullptr);
    file1.arr[index].metadata.atime = now;
    file1.arr[index].metadata.mtime = now;
    file1.arr[index].metadata.ctime = now;
    file1.arr[index].metadata.nlink = 1;
}

// Helper: recursively delete a subtree, O(1) unlink per node via prevsibling
static void delete_subtree_recursive(int index, treefile &file1, int max_depth = 1000) {
    if (index < 0 || index >= file1.size) return;
    if (file1.arr[index].isdeleted) return;
    if (max_depth <= 0) return;

    // Recursively delete all children first
    int child = file1.arr[index].firstchild;
    int iteration_count = 0;
    while (child != -1 && child < file1.size) {
        if (child < 0 || child >= file1.size) break;
        if (++iteration_count > file1.size) break;

        int nextSibling = file1.arr[child].nextsibling;
        delete_subtree_recursive(child, file1, max_depth - 1);
        child = nextSibling;
    }

    // Get filename before clearing metadata
    string filename = file1.arr[index].metadata.name;

    // O(1) unlink from parent's sibling list using prevsibling
    int parentIndex = file1.arr[index].parent;
    if (parentIndex >= 0 && parentIndex < file1.size) {
        int prev = file1.arr[index].prevsibling;
        int next = file1.arr[index].nextsibling;

        if (prev >= 0 && prev < file1.size) {
            file1.arr[prev].nextsibling = next;
        } else {
            // This node is the first child
            file1.arr[parentIndex].firstchild = next;
        }
        if (next >= 0 && next < file1.size) {
            file1.arr[next].prevsibling = prev;
        }
    }

    // Remove from hash map
    if (!filename.empty()) {
        hashmap_remove(&file1.hashdata, filename.c_str());
    }

    // Push to free list head
    file1.arr[index].nextfree = file1.firstfree;
    file1.firstfree = index;
    file1.arr[index].isdeleted = true;

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
    file1.arr[index].prevsibling = -1;
    file1.arr[index].parent = -1;

    // Note: nodeallocated is NOT decremented (it's a bump allocator high-water mark)
}

void delete1(string filename, treefile &file1){
    lock_guard<recursive_mutex> lock(treefile_mtx);

    if (filename.empty()) return;
    if (file1.size <= 0 || file1.size > TREEFILE_MAX_NODES) return;

    if (!hashmap_has(&file1.hashdata, filename.c_str())) return;

    int index = hashmap_get(&file1.hashdata, filename.c_str());
    if (index < 0 || index >= file1.size) return;

    // Prevent deletion of root node (index 0)
    if (index == 0) return;

    if (file1.arr[index].isdeleted) {
        // Node already deleted but hash entry exists — cleanup
        hashmap_remove(&file1.hashdata, filename.c_str());
        return;
    }

    // Recursively delete the entire subtree
    delete_subtree_recursive(index, file1, file1.size);
}

void change_parent(string filename, string newparentname, treefile &file1){
    lock_guard<recursive_mutex> lock(treefile_mtx);

    if (filename.empty()) return;
    if (file1.size <= 0 || file1.size > TREEFILE_MAX_NODES) return;

    if (!hashmap_has(&file1.hashdata, filename.c_str())) return;

    int index = hashmap_get(&file1.hashdata, filename.c_str());
    if (index < 0 || index >= file1.size || file1.arr[index].isdeleted) return;

    // Cannot change parent of root node
    if (index == 0) return;

    // Get new parent index (empty means root at index 0)
    int newparentindex = -1;
    if (newparentname.empty()) {
        newparentindex = 0;
    } else if (hashmap_has(&file1.hashdata, newparentname.c_str())) {
        newparentindex = hashmap_get(&file1.hashdata, newparentname.c_str());
        if (newparentindex < 0 || newparentindex >= file1.size ||
            file1.arr[newparentindex].isdeleted) {
            return;
        }
    } else {
        return;
    }

    // Check if moving to the same parent
    if (file1.arr[index].parent == newparentindex) return;

    // Prevent cycles
    if (newparentindex == index) return;

    int check = file1.arr[newparentindex].parent;
    int depth_limit = file1.size;
    while (check != -1 && check < file1.size && depth_limit > 0) {
        if (check == index) return;
        check = file1.arr[check].parent;
        depth_limit--;
    }

    // O(1) unlink from old parent using prevsibling
    int oldparentindex = file1.arr[index].parent;
    if (oldparentindex >= 0 && oldparentindex < file1.size) {
        int prev = file1.arr[index].prevsibling;
        int next = file1.arr[index].nextsibling;

        if (prev >= 0 && prev < file1.size) {
            file1.arr[prev].nextsibling = next;
        } else {
            file1.arr[oldparentindex].firstchild = next;
        }
        if (next >= 0 && next < file1.size) {
            file1.arr[next].prevsibling = prev;
        }
    }

    // Link to new parent (prepend) with prevsibling maintenance
    int firstson = file1.arr[newparentindex].firstchild;
    file1.arr[newparentindex].firstchild = index;

    file1.arr[index].nextsibling = firstson;
    file1.arr[index].prevsibling = -1;
    if (firstson >= 0 && firstson < file1.size) {
        file1.arr[firstson].prevsibling = index;
    }
    file1.arr[index].parent = newparentindex;
}

void initialize(treefile &file1){
    lock_guard<recursive_mutex> lock(treefile_mtx);

    // Set header fields (touches only the header page)
    file1.firstfree = -1;           // No pre-built free list (bump allocator)
    file1.start = -1;
    file1.size = TREEFILE_MAX_NODES;
    file1.nodeallocated = 1;         // Index 0 reserved for root

    // Set up root node at index 0 (touches only 1-2 pages)
    // All other nodes remain zero-filled from ftruncate — never touched.
    file1.arr[0].isdeleted = false;
    file1.arr[0].parent = -1;
    file1.arr[0].firstchild = -1;
    file1.arr[0].nextsibling = -1;
    file1.arr[0].prevsibling = -1;
    file1.arr[0].nextfree = -1;
    strncpy(file1.arr[0].metadata.name, "/",
            sizeof(file1.arr[0].metadata.name) - 1);
    file1.arr[0].metadata.name[sizeof(file1.arr[0].metadata.name) - 1] = '\0';
    file1.arr[0].metadata.mode = S_IFDIR | 0755;
    file1.arr[0].metadata.uid = getuid();
    file1.arr[0].metadata.gid = getgid();
    file1.arr[0].metadata.size = 0;
    file1.arr[0].metadata.nlink = 2;
    time_t now = time(nullptr);
    file1.arr[0].metadata.atime = now;
    file1.arr[0].metadata.mtime = now;
    file1.arr[0].metadata.ctime = now;
    file1.arr[0].metadata.inode = 1;

    // Clear hash map (no-op on fresh zero-filled files, needed for re-initialization)
    hashmap_clear(&file1.hashdata);

    // Add root to hash
    hashmap_set(&file1.hashdata, "/", 0);
}

// ============================================================
// mmap persistence
// ============================================================

bool mmap_init_treefile(const char* filepath, treefile*& ptr, int& fd_out, size_t& mapsize_out) {
    if (!filepath) return false;

    size_t file_size = sizeof(treefile);
    bool needs_init = false;

    // Try to open existing file
    int fd = open(filepath, O_RDWR);
    if (fd == -1) {
        // File doesn't exist — create it
        fd = open(filepath, O_RDWR | O_CREAT, 0644);
        if (fd == -1) return false;

        // Zero-fill via ftruncate (kernel allocates pages lazily, no page faults)
        if (ftruncate(fd, file_size) == -1) {
            close(fd);
            return false;
        }
        needs_init = true;
    } else {
        // File exists — check size
        struct stat st;
        if (fstat(fd, &st) == -1) {
            close(fd);
            return false;
        }
        if ((size_t)st.st_size != file_size) {
            // Incompatible size (version change) — reinitialize
            if (ftruncate(fd, file_size) == -1) {
                close(fd);
                return false;
            }
            needs_init = true;
        }
    }

    // Map file into memory
    void* mapped = mmap(NULL, file_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return false;
    }

    ptr = (treefile*)mapped;
    fd_out = fd;
    mapsize_out = file_size;

    if (needs_init) {
        // Fresh file — initialize root node and header
        initialize(*ptr);
    } else {
        // Existing file — verify it was properly initialized
        if (ptr->size != TREEFILE_MAX_NODES || ptr->nodeallocated < 1) {
            initialize(*ptr);
        }
    }

    return true;
}

void mmap_close_treefile(treefile* ptr, int fd, size_t mapsize) {
    if (ptr) {
        msync(ptr, mapsize, MS_SYNC);
        munmap(ptr, mapsize);
    }
    if (fd != -1) close(fd);
}

void mmap_sync_treefile(treefile* ptr, size_t mapsize) {
    if (ptr) {
        msync(ptr, mapsize, MS_ASYNC);
    }
}
