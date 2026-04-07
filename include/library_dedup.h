#ifndef LIBRARY_DEDUP_H
#define LIBRARY_DEDUP_H

#include <string>
#include <unordered_map>
#include "daemon/directory tree/adt.h"

// Initialize the library catalog (loads from disk)
void init_library_catalog(const std::string& config_file_path);

// Evaluate folder to see if it is a library and deduplicate if needed
void evaluate_and_deduplicate_library_folder(int folder_idx);

// Stub predictive ML model function
bool check_model_is_library(const std::string& folder_path);

#endif
