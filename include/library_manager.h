#ifndef FASTDEVFS_LIBRARY_MANAGER_H
#define FASTDEVFS_LIBRARY_MANAGER_H

/*
 * FastDevFs Library Manager — tracked library whitelist system.
 *
 * Manages a list of known library/package names stored in a JSON file.
 * Used to determine which folders should receive library-specific
 * deduplication treatment (node-sharing, etc.).
 *
 * File format (tracked_libraries.json):
 *   { "tracked_libraries": ["react", "vue", ...] }
 *
 * Thread-safe for all operations.
 */

#include <string>
#include <vector>

// Default path relative to project root
#define FASTDEVFS_DEFAULT_TRACKED_LIBS_PATH "./tracked_libraries.json"

// Check if a library name exists in the tracked list.
bool is_tracked_library(const std::string& name,
                        const std::string& path = FASTDEVFS_DEFAULT_TRACKED_LIBS_PATH);

// Add a library name to the tracked list.
// Returns true if added, false if it already exists.
bool add_tracked_library(const std::string& name,
                         const std::string& path = FASTDEVFS_DEFAULT_TRACKED_LIBS_PATH);

// Remove a library name from the tracked list.
// Returns true if removed, false if not found.
bool remove_tracked_library(const std::string& name,
                            const std::string& path = FASTDEVFS_DEFAULT_TRACKED_LIBS_PATH);

// Get all tracked library names (sorted).
std::vector<std::string> get_all_tracked_libraries(
    const std::string& path = FASTDEVFS_DEFAULT_TRACKED_LIBS_PATH);

#endif /* FASTDEVFS_LIBRARY_MANAGER_H */
