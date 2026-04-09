#ifndef FASTDEVFS_CONFIG_H
#define FASTDEVFS_CONFIG_H

/*
 * FastDevFs Configuration System
 *
 * INI-based persistent configuration with defaults.
 * Thread-safe for concurrent reads; writes are serialized.
 */

#include <string>
#include <map>
#include <mutex>

// Default paths
#define FASTDEVFS_DEFAULT_CONFIG_PATH  "./config.ini"
#define FASTDEVFS_DEFAULT_DATA_DIR     "/tmp/fastdevfs_data"
#define FASTDEVFS_DEFAULT_PERSIST_PATH "/tmp/fastdevfs.mmap"
#define FASTDEVFS_DEFAULT_DEDUP_PATH   "/tmp/fastdevfs_dedup.mmap"
#define FASTDEVFS_DEFAULT_SOCKET_PATH  "/tmp/fastdevfs_cli.sock"
#define FASTDEVFS_DEFAULT_PID_PATH     "/tmp/fastdevfs.pid"

struct FastDevFsConfig {
    // Filesystem paths
    std::string data_dir       = FASTDEVFS_DEFAULT_DATA_DIR;
    std::string persist_path   = FASTDEVFS_DEFAULT_PERSIST_PATH;
    std::string dedup_path     = FASTDEVFS_DEFAULT_DEDUP_PATH;
    std::string socket_path    = FASTDEVFS_DEFAULT_SOCKET_PATH;
    std::string pid_path       = FASTDEVFS_DEFAULT_PID_PATH;
    std::string mountpoint;

    // Deduplication
    bool        dedup_enabled  = true;
    std::string hash_algorithm = "sha256";
    int         chunk_size     = 4096;

    // Runtime
    std::string log_level      = "info";
    int         max_threads    = 4;
    bool        daemon_mode    = false;
};

// ---- Config API ----

// Load config from INI file. If file does not exist, creates it with defaults.
// Returns populated config. Thread-safe.
FastDevFsConfig load_config(const std::string& path = FASTDEVFS_DEFAULT_CONFIG_PATH);

// Save config to INI file. Creates parent directories if needed.
bool save_config(const FastDevFsConfig& config,
                 const std::string& path = FASTDEVFS_DEFAULT_CONFIG_PATH);

// Get a config value by key name (string-based, for CLI use).
// Returns empty string if key not found.
std::string get_config_value(const FastDevFsConfig& config, const std::string& key);

// Set a config value by key name (string-based, for CLI use).
// Returns true if key was recognized and set, false otherwise.
bool set_config_value(FastDevFsConfig& config,
                      const std::string& key, const std::string& value);

// Return a config with all default values.
FastDevFsConfig get_default_config();

// List all config keys and their current values.
std::map<std::string, std::string> config_to_map(const FastDevFsConfig& config);

// ---- Global config (used by daemon) ----

// Set/get the global in-memory config (thread-safe).
void set_global_config(const FastDevFsConfig& config);
FastDevFsConfig get_global_config();

// Update a single key in the global config (thread-safe).
bool update_global_config(const std::string& key, const std::string& value);

#endif /* FASTDEVFS_CONFIG_H */
