#pragma once

#include "../include/dedup_index.h"
#include <cstdint>
#include <atomic>

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

// Register a callback for library dedup evaluation.
// Called by daemon on startup to hook in evaluate_and_deduplicate_library_folder.
// CLI does not call this, so directory settlement timers are safely skipped.
void register_library_dedup_callback(void (*fn)(int));

// ---- Full Dedup Pass (triggered via CLI) ----

// Trigger a full filesystem dedup pass (full scan and deduplication).
// This scans all files in FASTDEVFS_DATA_DIR and applies dedup logic.
// Returns immediately; progress can be queried via get_dedup_pass_progress().
void trigger_dedup_pass();

// Get progress of the currently running dedup pass.
// Returns true if a pass is running, false otherwise.
// Fills in: processed, total, duplicates, saved_bytes
bool get_dedup_pass_progress(int& processed, int& total, int& duplicates, uint64_t& saved_bytes);

// Check if a dedup pass is currently running.
bool is_dedup_pass_running();

// ---- Dedup Statistics Query ----

// Get deduplicated files (files with refcount > 1)
// Returns a string with pipe-delimited entries: "file_index=refcount|..."
// Also fills in total_duplicates and total_saved_bytes
std::string get_deduplicated_files_info(int& total_duplicates, uint64_t& total_saved_bytes);
