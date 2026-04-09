/*
 * FastDevFs Library Manager — tracked library whitelist implementation.
 *
 * Provides safe read/write of the tracked_libraries.json file.
 * Uses a minimal hand-rolled JSON parser (no external dependency)
 * consistent with the project's existing JSON approach in ipc.cpp.
 *
 * File format:
 *   { "tracked_libraries": ["name1", "name2", ...] }
 */

#include "../include/library_manager.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <mutex>
#include <cctype>

// ============================================================
// Internal helpers
// ============================================================

static std::mutex g_lib_mgr_mutex;

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

// ============================================================
// JSON read: parse "tracked_libraries" array from file
// ============================================================

static std::vector<std::string> read_tracked_libs(const std::string& path) {
    std::vector<std::string> libs;

    std::ifstream file(path);
    if (!file.is_open()) {
        return libs;  // File doesn't exist yet — return empty
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    file.close();
    std::string content = ss.str();

    // Find the array after "tracked_libraries"
    size_t key_pos = content.find("\"tracked_libraries\"");
    if (key_pos == std::string::npos) {
        return libs;
    }

    // Find the opening '['
    size_t arr_start = content.find('[', key_pos);
    if (arr_start == std::string::npos) {
        return libs;
    }

    // Find the closing ']'
    size_t arr_end = content.find(']', arr_start);
    if (arr_end == std::string::npos) {
        return libs;
    }

    // Extract strings between quotes
    std::string arr = content.substr(arr_start + 1, arr_end - arr_start - 1);
    size_t pos = 0;

    while (pos < arr.size()) {
        size_t quote_start = arr.find('"', pos);
        if (quote_start == std::string::npos) break;

        size_t quote_end = arr.find('"', quote_start + 1);
        if (quote_end == std::string::npos) break;

        std::string name = arr.substr(quote_start + 1, quote_end - quote_start - 1);
        if (!name.empty()) {
            libs.push_back(name);
        }

        pos = quote_end + 1;
    }

    return libs;
}

// ============================================================
// JSON write: persist library list to file (atomic via temp)
// ============================================================

static bool write_tracked_libs(const std::vector<std::string>& libs,
                                const std::string& path) {
    // Write to temp file first, then rename for atomicity
    std::string tmp_path = path + ".tmp";

    std::ofstream file(tmp_path, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file << "{\n  \"tracked_libraries\": [\n";

    // Write entries, ~8 per line for readability (matching original format)
    for (size_t i = 0; i < libs.size(); i++) {
        if (i % 8 == 0) {
            file << "    ";
        }

        file << "\"" << libs[i] << "\"";

        if (i + 1 < libs.size()) {
            file << ", ";
            if ((i + 1) % 8 == 0) {
                file << "\n";
            }
        }
    }

    file << "\n  ]\n}\n";
    file.close();

    // Atomic rename
    if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
        // Fallback: remove tmp
        std::remove(tmp_path.c_str());
        return false;
    }

    return true;
}

// ============================================================
// Ensure file exists with default structure
// ============================================================

static void ensure_file_exists(const std::string& path) {
    std::ifstream check(path);
    if (check.is_open()) {
        check.close();
        return;
    }

    // Create with empty list
    std::vector<std::string> empty;
    write_tracked_libs(empty, path);
}

// ============================================================
// Public API
// ============================================================

bool is_tracked_library(const std::string& name, const std::string& path) {
    std::lock_guard<std::mutex> lock(g_lib_mgr_mutex);
    ensure_file_exists(path);

    std::string search = to_lower(trim(name));
    if (search.empty()) return false;

    auto libs = read_tracked_libs(path);
    for (const auto& lib : libs) {
        if (to_lower(lib) == search) {
            return true;
        }
    }
    return false;
}

bool add_tracked_library(const std::string& name, const std::string& path) {
    std::lock_guard<std::mutex> lock(g_lib_mgr_mutex);
    ensure_file_exists(path);

    std::string trimmed = trim(name);
    if (trimmed.empty()) return false;

    auto libs = read_tracked_libs(path);

    // Check for duplicate (case-insensitive)
    std::string lower = to_lower(trimmed);
    for (const auto& lib : libs) {
        if (to_lower(lib) == lower) {
            return false;  // Already exists
        }
    }

    libs.push_back(trimmed);
    return write_tracked_libs(libs, path);
}

bool remove_tracked_library(const std::string& name, const std::string& path) {
    std::lock_guard<std::mutex> lock(g_lib_mgr_mutex);
    ensure_file_exists(path);

    std::string search = to_lower(trim(name));
    if (search.empty()) return false;

    auto libs = read_tracked_libs(path);
    size_t original_size = libs.size();

    libs.erase(
        std::remove_if(libs.begin(), libs.end(),
                        [&search](const std::string& lib) {
                            std::string lower = lib;
                            std::transform(lower.begin(), lower.end(),
                                           lower.begin(), ::tolower);
                            return lower == search;
                        }),
        libs.end()
    );

    if (libs.size() == original_size) {
        return false;  // Not found
    }

    return write_tracked_libs(libs, path);
}

std::vector<std::string> get_all_tracked_libraries(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_lib_mgr_mutex);
    ensure_file_exists(path);

    auto libs = read_tracked_libs(path);
    std::sort(libs.begin(), libs.end(), [](const std::string& a, const std::string& b) {
        std::string la = a, lb = b;
        std::transform(la.begin(), la.end(), la.begin(), ::tolower);
        std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);
        return la < lb;
    });
    return libs;
}
