#pragma once

#include <stdbool.h>
#include <pthread.h>
#include "daemon/hash.h"

#define MAX_NODES 10000
#define NAME_SIZE 256
#define DIR_MANAGER_MAGIC 0xDEADBEEF

typedef struct DirNode {
    char name[NAME_SIZE];
    int parent;
    int first_child;
    int next_sibling;

    int inode;

    bool in_use;
    int next_free;
} DirNode;

typedef struct DirManager {
    unsigned int magic;  // Magic number to detect initialization
    DirNode nodes[MAX_NODES];
    int root;
    int free_list;
    pthread_rwlock_t rwlock;
} DirManager;

// API
void dir_manager_init(DirManager* dm);
int  insert_node(DirManager* dm, const char* path);
int  lookup_node(DirManager* dm, const char* path);
bool remove_node(DirManager* dm, const char* path);
bool is_dir_manager_initialized(DirManager* dm);
