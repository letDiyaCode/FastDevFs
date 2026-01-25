#pragma once

#include <stdbool.h>
#include <pthread.h>
#include "daemon/hash.h"

#define MAX_NODES 10000
#define NAME_SIZE 256

// directory node
typedef struct DirNode {
    char name[NAME_SIZE];
    int parent;
    int first_child;
    int next_sibling;
    bool in_use;
    int next_free;
} DirNode;

// directory manager
typedef struct DirManager {
    DirNode nodes[MAX_NODES];
    int root;
    int free_list;
    HashTable hash;
    pthread_rwlock_t rwlock;
} DirManager;

// API
void dir_manager_init(DirManager* dm);
int  insert_node(DirManager* dm, const char* path);
int  lookup_node(DirManager* dm, const char* path);
bool remove_node(DirManager* dm, const char* path);
