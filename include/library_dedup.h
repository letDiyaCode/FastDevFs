#ifndef LIBRARY_DEDUP_H
#define LIBRARY_DEDUP_H

#include <string>
#include <unordered_map>
#include "daemon/directory tree/adt.h"

// Initialize the library catalog (loads canonical folder hashes from disk)
void init_library_catalog(const std::string& config_file_path);

// Initialize the tracked libraries sorted vector from tracked_libraries.json
void init_tracked_libraries(const std::string& json_path);

// Initialize the MLP-based library predictor model
void init_predictor(const std::string& model_path);

// Evaluate folder to see if it is a library and deduplicate if needed
void evaluate_and_deduplicate_library_folder(int folder_idx);

// Two-tier library detection:
//   Tier 1: binary search in tracked_libraries.json (O(log n))
//   Tier 2: MLP predictor fallback (appends to JSON on discovery)
bool check_model_is_library(const std::string& folder_path);

#endif
