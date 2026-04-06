#ifndef ADT_H
#define ADT_H

#include <iostream>
#include <mutex>
#include <string>
#include "hash.h"
using namespace std;

#define TREEFILE_MAX_NODES 100000 // Maximum number of nodes in the tree

struct metadate{
    int inode = -1;
    char name[300] = "";  // Full absolute path (e.g. "/dir/file.txt")
    mode_t mode;
    uid_t  uid;
    gid_t  gid;
    off_t  size;
    time_t atime;
    time_t mtime;
    time_t ctime;
    nlink_t nlink;
    bool is_library = false;  // true if this file belongs to a library/dependency
};

struct treenode{
    int nextfree = -1;
    int firstchild = -1;
    int nextsibling = -1;
    int prevsibling = -1;   // Doubly linked sibling list for O(1) unlink
    int parent = -1;
    metadate metadata;
    bool isdeleted = true;
};

// Single treefile struct — this IS the mmap layout.
// Contains only POD types so it can be directly mmap'd.
struct treefile{
    int firstfree;
    int start;
    int size;
    int nodeallocated;           // Bump allocator high-water mark (never decremented)
    hashmap_t hashdata;          // Hash table for O(1) name→index lookup
    treenode arr[TREEFILE_MAX_NODES];
};

// Global recursive mutex for thread-safe ADT operations (not in mmap)
extern recursive_mutex treefile_mtx;

// ADT operations (all take treefile& pointing into mmap'd memory)
// All path arguments are full absolute paths (e.g. "/dir/file.txt")
int hashindex(string filepath, treefile &file1);
void insertfile(string filepath, string parentpath, treefile &file1);
void insertfolder(string folderpath, string parentpath, treefile &file1);
void delete1(string filepath, treefile &file1);
void change_parent(string filepath, string newparentpath, treefile &file1);
void initialize(treefile &file1);

// mmap persistence
bool mmap_init_treefile(const char* filepath, treefile*& ptr, int& fd, size_t& mapsize);
void mmap_close_treefile(treefile* ptr, int fd, size_t mapsize);
void mmap_sync_treefile(treefile* ptr, size_t mapsize);

#endif /* ADT_H */
