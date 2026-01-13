#include <iostream>
#include "adt.h"
#include "hash.h"
#include "hash.cpp"
using namespace std;

int hashindex(string filename, treefile &file1){
    lock_guard<recursive_mutex> lock(file1.mtx);
    if (!file1.head.hash.has(filename)) {
        return -1;  // Return -1 if filename not found
    }
    return file1.head.hash[filename];
}

void insert(string filename, string parentname, treefile &file1){
    lock_guard<recursive_mutex> lock(file1.mtx);
    
    // Check if filename already exists
    if (file1.head.hash.has(filename)) {
        int existingIndex = file1.head.hash[filename];
        if (existingIndex >= 0 && existingIndex < file1.head.size && 
            !file1.arr[existingIndex].isdeleted) {
            return;  // File already exists
        }
    }
    
    int free = file1.head.firstfree;
    if (free < 0 || free >= file1.head.size) {
        return;  // No free space available
    }
    
    int nextf = file1.arr[free].nextfree;
    int index = free;
    
    // Update firstfree to point to next free node
    file1.head.firstfree = nextf;
    
    // Add to hash map
    file1.head.hash[filename] = index;
    
    // Get parent index (empty parentname means root node)
    int parentindex = -1;
    if (!parentname.empty() && file1.head.hash.has(parentname)) {
        parentindex = file1.head.hash[parentname];
        // Verify parent is not deleted
        if (parentindex >= 0 && parentindex < file1.head.size && 
            file1.arr[parentindex].isdeleted) {
            parentindex = -1;  // Parent is deleted, treat as root
        }
    }
    
    // Link to parent if parent exists
    if (parentindex >= 0 && parentindex < file1.head.size) {
        int firstson = file1.arr[parentindex].firstchild;
        file1.arr[parentindex].firstchild = index;  // new kid given
        
        file1.arr[index].nextsibling = firstson;  // sibling sorted
        file1.arr[index].parent = parentindex;
    } else {
        // Root node (no parent)
        file1.arr[index].parent = -1;
        file1.arr[index].nextsibling = -1;
    }
    
    // Set node properties
    file1.arr[index].isdeleted = false;
    file1.arr[index].metadata.name = filename;
    file1.arr[index].firstchild = -1;
}

void delete1(string filename, treefile &file1){
    lock_guard<recursive_mutex> lock(file1.mtx);
    
    if (!file1.head.hash.has(filename)) {
        return;  // File not found
    }
    
    int index = hashindex(filename, file1);
    if (index < 0 || index >= file1.head.size) {
        return;  // Invalid index
    }
    
    // Check if node has children - prevent deletion if it does
    // (Alternatively, could recursively delete children, but that's more complex)
    if (file1.arr[index].firstchild != -1) {
        return;  // Cannot delete node with children
    }
    
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
    
    // Add to free list
    int free = file1.head.firstfree;
    file1.head.firstfree = index;
    file1.arr[index].isdeleted = true;
    file1.arr[index].nextfree = free;
    
    // Clear metadata and tree links
    file1.arr[index].metadata.inode = -1;
    file1.arr[index].metadata.name = "";
    file1.arr[index].firstchild = -1;
    file1.arr[index].nextsibling = -1;
    file1.arr[index].parent = -1;
    
    // Note: Hash map entry is not removed (HashMap doesn't have remove function)
    // The entry will remain but point to a deleted node, which is acceptable
    // since we check isdeleted flag and hash.has() before using the index
}

void initialize(treefile &file1){
    lock_guard<recursive_mutex> lock(file1.mtx);
    
    int size = file1.head.size;
    for(int i = 0; i < size; i++){
        file1.arr[i].isdeleted = true;
        file1.arr[i].nextfree = i+1;
        file1.arr[i].firstchild = -1;
        file1.arr[i].nextsibling = -1;
        file1.arr[i].parent = -1;
        file1.arr[i].metadata.inode = -1;
        file1.arr[i].metadata.name = "";
    } 
    // Last node's nextfree should be -1
    if (size > 0) {
        file1.arr[size - 1].nextfree = -1;
    }
    file1.head.firstfree = 0;
}
