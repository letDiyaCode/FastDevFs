#include "daemon/dir_manager.h"
#include <string.h>
#include <cstdio>

void dir_manager_init(DirManager* dm) {
    hash_init(&dm->hash);
    pthread_rwlock_init(&dm->rwlock, NULL);

    dm->root = 0;
    dm->nodes[0].name[0] = '/';
    dm->nodes[0].parent = -1;
    dm->nodes[0].first_child = -1;
    dm->nodes[0].next_sibling = -1;
    dm->nodes[0].in_use = true;

    dm->free_list = 1;
    for (int i = 1; i < MAX_NODES - 1; i++) {
        dm->nodes[i].next_free = i + 1;
        dm->nodes[i].in_use = false;
    }
    dm->nodes[MAX_NODES - 1].next_free = -1;
}

int lookup_node(DirManager* dm, const char* path) {
    pthread_rwlock_rdlock(&dm->rwlock);

    if (strcmp(path, "/") == 0) {
        pthread_rwlock_unlock(&dm->rwlock);
        return dm->root;
    }

    uint64_t h = hash_path_poly(path);
    int res = hash_lookup(&dm->hash, h, path);

    pthread_rwlock_unlock(&dm->rwlock);
    return res;
}

int insert_node(DirManager* dm, const char* path) {
    pthread_rwlock_wrlock(&dm->rwlock);

    if (strcmp(path, "/") == 0) {
        pthread_rwlock_unlock(&dm->rwlock);
        return -1;
    }

    int parent = dm->root;
    char token[NAME_SIZE];
    int ti = 0;

    for (const char* p = path; ; ++p) {
        if (*p == '/' || *p == '\0') {
            if (ti > 0) {
                token[ti] = '\0';

                char full_key[KEY_SIZE];
                snprintf(full_key, KEY_SIZE, "%.*s",
                         (int)(p - path), path);

                uint64_t h = hash_path_poly(full_key);
                int node = hash_lookup(&dm->hash, h, full_key);

                if (*p == '\0') {
                    if (node != -1 || dm->free_list == -1) {
                        pthread_rwlock_unlock(&dm->rwlock);
                        return -1;
                    }

                    int new_node = dm->free_list;
                    dm->free_list = dm->nodes[new_node].next_free;

                    DirNode* n = &dm->nodes[new_node];
                    strncpy(n->name, token, NAME_SIZE);
                    n->parent = parent;
                    n->first_child = -1;
                    n->next_sibling = dm->nodes[parent].first_child;
                    n->in_use = true;

                    dm->nodes[parent].first_child = new_node;
                    hash_insert(&dm->hash, h, full_key, new_node);

                    pthread_rwlock_unlock(&dm->rwlock);
                    return new_node;
                }

                if (node == -1) {
                    pthread_rwlock_unlock(&dm->rwlock);
                    return -1;
                }

                parent = node;
                ti = 0;
            }
            if (*p == '\0')
                break;
        } else if (ti < NAME_SIZE - 1) {
            token[ti++] = *p;
        }
    }

    pthread_rwlock_unlock(&dm->rwlock);
    return -1;
}

bool remove_node(DirManager* dm, const char* path) {
    pthread_rwlock_wrlock(&dm->rwlock);

    int node = lookup_node(dm, path);
    if (node <= 0) {
        pthread_rwlock_unlock(&dm->rwlock);
        return false;
    }

    DirNode* n = &dm->nodes[node];
    if (n->first_child != -1) {
        pthread_rwlock_unlock(&dm->rwlock);
        return false;
    }

    uint64_t h = hash_path_poly(path);

    int parent = n->parent;
    int* link = &dm->nodes[parent].first_child;
    while (*link != -1 && *link != node)
        link = &dm->nodes[*link].next_sibling;

    if (*link == node)
        *link = n->next_sibling;

    hash_remove(&dm->hash, h, path);

    n->in_use = false;
    n->next_free = dm->free_list;
    dm->free_list = node;

    pthread_rwlock_unlock(&dm->rwlock);
    return true;
}
