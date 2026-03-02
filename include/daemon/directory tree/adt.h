#ifndef ADT_H
#define ADT_H

#include <iostream>
#include <mutex>
#include <string>
#include "hash.h"
using namespace std;

#define MAX_FILE_DATA 4096   // Maximum inline file data per node (4 KB)

struct metadate{
    int inode = -1;
    char name[256] = "";  // Fixed-size char array instead of std::string for mmap compatibility
    mode_t mode;        // file type + permissions
    uid_t  uid;
    gid_t  gid;
    off_t  size;
    time_t atime;
    time_t mtime;
    time_t ctime;
    nlink_t nlink;
};
struct treenode{
    int nextfree= -1;
    int firstchild = -1;
    int nextsibling = -1;
    int parent = -1;
    metadate metadata;
    bool isdeleted = true;
    char data[MAX_FILE_DATA];   // Inline file content (persisted via mmap)
};
struct header{
  int firstfree = 0;
  int start = -1;
  int size = 100000;
  int nodeallocated = 1; ////// 0th index will be used for root node
   HashMap hash;
};
struct treefile{
    header head;
    treenode arr[100000];
    recursive_mutex mtx;  // Recursive mutex for thread-safe operations
};

int hashindex(string filename, treefile &file1);
void insertfile(string filename, string parentname, treefile &file1);
void insertfolder(string filename, string parentname, treefile &file1);
void delete1(string filename, treefile &file1);
void change_parent(string filename, string newparentname, treefile &file1);
void initialize(treefile &file1);

// Serializable versions for mmap persistence (excludes mutex and HashMap wrapper)
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

// Persistence functions using mmap
bool save_treefile(const char* filepath, treefile &file1);
bool load_treefile(const char* filepath, treefile &file1);
bool init_or_load_treefile(const char* filepath, treefile &file1);


#endif /* ADT_H */
