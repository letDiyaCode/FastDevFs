#pragma once

#include "../include/dedup_index.h"

// Starts the dedup server threads (worker + timer).
// mmap_path: path for the persistent dedup index file.
void start_dedup_server(const char* dedup_mmap_path);

// Stops the dedup server gracefully.
void stop_dedup_server();

// ---- Synchronous functions called from FUSE thread ----

// Check if a file is currently sharing data with another file (refcount > 1).
bool is_file_shared(uint64_t inode);

// Perform Copy-on-Write break for a shared file.
// file_index: tree array index of the file
// fd: reference to the open file descriptor (will be closed and reopened)
// Returns 0 on success, -1 on error.
int dedup_cow_break(int file_index, int& fd);

// Notify dedup system of file deletion (decrement refcount).
// Must be called BEFORE the physical data file is unlinked.
void dedup_notify_delete(int file_index);

// ---- Policy ----

void set_dedup_policy(DedupPolicy policy);
DedupPolicy get_dedup_policy();

// Check if a dedup request should be sent based on current policy.
bool should_dedup(bool is_library);
