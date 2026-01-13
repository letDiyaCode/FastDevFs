#ifndef ADT_H
#define ADT_H

#include <iostream>
#include <mutex>
#include <string>
#include "hash.h"
using namespace std;

struct metadate{
int inode = -1;
string name = "";
};
struct treenode{
    int nextfree= -1;
    
    bool isdeleted = true;
    
    int firstchild = -1;
    int nextsibling = -1;
    int parent = -1;
    metadate metadata;
};
struct header{
  int firstfree = 0;
  int start = -1;
  int size = 100000;
   HashMap hash;
};
struct treefile{
    header head;
    treenode arr[100000];
    recursive_mutex mtx;  // Recursive mutex for thread-safe operations
};

int hashindex(string filename, treefile &file1);
void insert(string filename, string parentname, treefile &file1);
void delete1(string filename, treefile &file1);
void initialize(treefile &file1);

#endif /* ADT_H */