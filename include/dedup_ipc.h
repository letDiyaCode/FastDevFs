#pragma once
#include <stdint.h>

#define DEDUP_SOCKET_PATH "/tmp/fastdevfs_dedup.sock"

struct DedupRequest {
    uint64_t inode;
    char path[512];
    int operation_type; // 1 for insertion, 2 for update
};
