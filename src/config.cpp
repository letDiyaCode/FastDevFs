/*
 * FastDevFs Configuration System — INI parser/writer implementation.
 *
 * Format:
 *   key = value
 *   # comments
 *   blank lines ignored
 *
 * No sections needed — flat key/value for simplicity.
 */

#include "../include/config.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

// ============================================================
// Helpers
// ============================================================

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

static bool parse_bool(const std::string& s) {
    std::string lower = to_lower(trim(s));
    return (lower == "true" || lower == "1" || lower == "yes" || lower == "on");
}

static std::string bool_to_string(bool b) {
    return b ? "true" : "false";
}

// ============================================================
// Global config (thread-safe singleton)
// ============================================================

static std::mutex g_config_mutex;
static FastDevFsConfig g_config;
static bool g_config_initialized = false;

void set_global_config(const FastDevFsConfig& config) {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    g_config = config;
    g_config_initialized = true;
}

FastDevFsConfig get_global_config() {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    if (!g_config_initialized) {
        g_config = get_default_config();
        g_config_initialized = true;
    }
    return g_config;
}

bool update_global_config(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    if (!g_config_initialized) {
        g_config = get_default_config();
        g_config_initialized = true;
    }
    return set_config_value(g_config, key, value);
}

// ============================================================
// Config ↔ Map conversion
// ============================================================

std::map<std::string, std::string> config_to_map(const FastDevFsConfig& config) {
    std::map<std::string, std::string> m;
    m["data_dir"]       = config.data_dir;
    m["persist_path"]   = config.persist_path;
    m["dedup_path"]     = config.dedup_path;
    m["socket_path"]    = config.socket_path;
    m["pid_path"]       = config.pid_path;
    m["mountpoint"]     = config.mountpoint;
    m["dedup_enabled"]  = bool_to_string(config.dedup_enabled);
    m["hash_algorithm"] = config.hash_algorithm;
    m["chunk_size"]     = std::to_string(config.chunk_size);
    m["log_level"]      = config.log_level;
    m["max_threads"]    = std::to_string(config.max_threads);
    m["daemon_mode"]    = bool_to_string(config.daemon_mode);
    return m;
}

static void map_to_config(const std::map<std::string, std::string>& m,
                           FastDevFsConfig& config) {
    for (const auto& [key, value] : m) {
        set_config_value(config, key, value);
    }
}

// ============================================================
// String-based get/set
// ============================================================

std::string get_config_value(const FastDevFsConfig& config, const std::string& key) {
    std::string k = to_lower(trim(key));

    if (k == "data_dir")       return config.data_dir;
    if (k == "persist_path")   return config.persist_path;
    if (k == "dedup_path")     return config.dedup_path;
    if (k == "socket_path")    return config.socket_path;
    if (k == "pid_path")       return config.pid_path;
    if (k == "mountpoint")     return config.mountpoint;
    if (k == "dedup_enabled")  return bool_to_string(config.dedup_enabled);
    if (k == "hash_algorithm") return config.hash_algorithm;
    if (k == "chunk_size")     return std::to_string(config.chunk_size);
    if (k == "log_level")      return config.log_level;
    if (k == "max_threads")    return std::to_string(config.max_threads);
    if (k == "daemon_mode")    return bool_to_string(config.daemon_mode);

    return "";  // Unknown key
}

bool set_config_value(FastDevFsConfig& config,
                      const std::string& key, const std::string& value) {
    std::string k = to_lower(trim(key));
    std::string v = trim(value);

    if (k == "data_dir")       { config.data_dir       = v; return true; }
    if (k == "persist_path")   { config.persist_path   = v; return true; }
    if (k == "dedup_path")     { config.dedup_path     = v; return true; }
    if (k == "socket_path")    { config.socket_path    = v; return true; }
    if (k == "pid_path")       { config.pid_path       = v; return true; }
    if (k == "mountpoint")     { config.mountpoint     = v; return true; }
    if (k == "dedup_enabled")  { config.dedup_enabled  = parse_bool(v); return true; }
    if (k == "hash_algorithm") { config.hash_algorithm = v; return true; }
    if (k == "log_level")      { config.log_level      = v; return true; }
    if (k == "daemon_mode")    { config.daemon_mode    = parse_bool(v); return true; }

    if (k == "chunk_size") {
        try { config.chunk_size = std::stoi(v); return true; }
        catch (...) { return false; }
    }
    if (k == "max_threads") {
        try { config.max_threads = std::stoi(v); return true; }
        catch (...) { return false; }
    }

    return false;  // Unknown key
}

// ============================================================
// Default config
// ============================================================

FastDevFsConfig get_default_config() {
    return FastDevFsConfig{};  // Struct defaults are already set
}

// ============================================================
// INI File I/O
// ============================================================

FastDevFsConfig load_config(const std::string& path) {
    FastDevFsConfig config = get_default_config();

    std::ifstream file(path);
    if (!file.is_open()) {
        // File doesn't exist — create with defaults
        std::cerr << "[Config] Config file not found at " << path
                  << ", creating with defaults." << std::endl;
        save_config(config, path);
        return config;
    }

    std::map<std::string, std::string> values;
    std::string line;
    int line_num = 0;

    while (std::getline(file, line)) {
        line_num++;
        std::string trimmed = trim(line);

        // Skip empty lines and comments
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        // Skip section headers like [section]
        if (trimmed[0] == '[') {
            continue;
        }

        // Parse key = value
        size_t eq_pos = trimmed.find('=');
        if (eq_pos == std::string::npos) {
            std::cerr << "[Config] Warning: malformed line " << line_num
                      << ": " << trimmed << std::endl;
            continue;
        }

        std::string key = trim(trimmed.substr(0, eq_pos));
        std::string value = trim(trimmed.substr(eq_pos + 1));

        if (!key.empty()) {
            values[to_lower(key)] = value;
        }
    }

    file.close();
    map_to_config(values, config);

    std::cout << "[Config] Loaded config from " << path << std::endl;
    return config;
}

bool save_config(const FastDevFsConfig& config, const std::string& path) {
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "[Config] Error: cannot write to " << path << std::endl;
        return false;
    }

    file << "# FastDevFs Configuration\n";
    file << "# Generated automatically — edit with care\n\n";

    file << "# Filesystem paths\n";
    file << "data_dir = "       << config.data_dir     << "\n";
    file << "persist_path = "   << config.persist_path  << "\n";
    file << "dedup_path = "     << config.dedup_path    << "\n";
    file << "socket_path = "    << config.socket_path   << "\n";
    file << "pid_path = "       << config.pid_path      << "\n";
    file << "mountpoint = "     << config.mountpoint    << "\n\n";

    file << "# Deduplication\n";
    file << "dedup_enabled = "  << bool_to_string(config.dedup_enabled) << "\n";
    file << "hash_algorithm = " << config.hash_algorithm << "\n";
    file << "chunk_size = "     << config.chunk_size     << "\n\n";

    file << "# Runtime\n";
    file << "log_level = "      << config.log_level      << "\n";
    file << "max_threads = "    << config.max_threads    << "\n";
    file << "daemon_mode = "    << bool_to_string(config.daemon_mode) << "\n";

    file.close();
    return true;
}
