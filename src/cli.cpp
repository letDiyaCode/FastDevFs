/*
 * FastDevFs CLI Tool — Production-quality command-line interface.
 *
 * Uses CLI11 for argument parsing.
 * Acts as management frontend for the FastDevFs daemon.
 *
 * Architecture:
 *   - CLI parses commands and routes to core modules
 *   - If daemon is running, commands are sent via IPC
 *   - If daemon is not running, operates directly on files/config
 */

#include "CLI/CLI.hpp"

#include "../include/config.h"
#include "../include/ipc.h"
#include "../include/sha256.h"
#include "../include/ui.h"
#include "../include/library_manager.h"

#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <csignal>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cctype>

namespace fs = std::filesystem;

// ============================================================
// Global state
// ============================================================

static bool g_verbose = false;
static std::string g_config_path = FASTDEVFS_DEFAULT_CONFIG_PATH;

// ============================================================
// Helper: print verbose messages
// ============================================================

static void vlog(const std::string& msg) {
    if (g_verbose) {
        ui::print_verbose(msg);
    }
}

// ============================================================
// Helper: resolve daemon binary path
// ============================================================

static std::string get_daemon_path() {
    // Strategy 1: Resolve relative to CLI binary via /proc/self/exe
    char exe_path[1024] = {0};
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    
    if (len > 0) {
        exe_path[len] = '\0';
        fs::path exe_fs_path(exe_path);
        fs::path exe_dir = exe_fs_path.parent_path();
        fs::path daemon_path = exe_dir / "fastdevfs";
        
        vlog("Resolved CLI executable: " + std::string(exe_path));
        vlog("Checking daemon at: " + daemon_path.string());
        
        if (fs::exists(daemon_path)) {
            return fs::canonical(daemon_path).string();
        }
    }

    // Strategy 2: Check ./build/fastdevfs (common dev layout)
    if (fs::exists("./build/fastdevfs")) {
        vlog("Found daemon in ./build/fastdevfs");
        return fs::canonical("./build/fastdevfs").string();
    }

    // Strategy 3: Check current directory
    if (fs::exists("./fastdevfs")) {
        vlog("Found daemon in ./fastdevfs");
        return fs::canonical("./fastdevfs").string();
    }

    // Not found
    vlog("Daemon binary 'fastdevfs' not found in any search path");
    return "";
}

// ============================================================
// Helper: validate mountpoint for FUSE mounting
// ============================================================

static bool validate_mountpoint_cli(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    // Check if directory exists
    if (!fs::exists(path)) {
        ui::print_error("Mountpoint '" + path + "' does not exist");
        std::cerr << "  Create it with: mkdir -p " << path << "\n" << std::endl;
        return false;
    }

    // Check if it's a directory
    if (!fs::is_directory(path)) {
        ui::print_error("Mountpoint '" + path + "' is not a directory");
        std::cerr << "  Please provide a directory path, not a file.\n" << std::endl;
        return false;
    }

    // Try to check permissions by attempting to list
    try {
        auto it = fs::directory_iterator(path);
        // Count entries (skip . and ..)
        int entry_count = 0;
        for (auto const& entry : fs::directory_iterator(path)) {
            entry_count++;
        }
        
        if (entry_count > 0) {
            ui::print_warning("Mountpoint '" + path + "' is not empty");
            std::cerr << "  FUSE typically requires an empty directory." << std::endl;
            std::cerr << "  This may cause mounting to fail.\n" << std::endl;
        }
    } catch (const fs::filesystem_error& e) {
        ui::print_error("Cannot access mountpoint '" + path + "'");
        std::cerr << "  " << e.what() << "\n" << std::endl;
        return false;
    }

    return true;
}

// ============================================================
// Helper: clean up stale daemon state (socket + PID files)
// ============================================================

static void cleanup_stale_state(const FastDevFsConfig& config) {
    // Check for stale PID file
    pid_t pid;
    if (read_pid_file(config.pid_path, pid)) {
        // Check if process is actually alive
        if (kill(pid, 0) != 0) {
            vlog("Removing stale PID file (pid " + std::to_string(pid) + " is dead)");
            remove_pid_file(config.pid_path);
        }
    }

    // Check for stale socket file
    if (fs::exists(config.socket_path)) {
        // Try connecting — if it fails, socket is stale
        std::string response = send_ipc_command(config.socket_path,
                                                 ipc_make_request("ping"));
        if (response.empty()) {
            vlog("Removing stale socket file: " + config.socket_path);
            unlink(config.socket_path.c_str());
        }
    }
}

// ============================================================

static void cmd_start(bool daemon_mode, const std::string& mountpoint_arg) {
    FastDevFsConfig config = load_config(g_config_path);

    // Check if already running
    if (is_daemon_running(config.socket_path)) {
        ui::print_error("FastDevFS daemon is already running");
        std::cerr << "  Use '" << ui::colored("fastdevfs-cli stop", ui::Color::YELLOW) 
                  << "' to stop it first.\n" << std::endl;
        exit(1);
    }

    // Clean up stale state from previous crashed runs
    cleanup_stale_state(config);

    // Determine mountpoint
    std::string mountpoint = mountpoint_arg;
    if (mountpoint.empty()) {
        mountpoint = config.mountpoint;
    }
    if (mountpoint.empty()) {
        ui::print_error("No mountpoint specified");
        std::cerr << "  Use: " << ui::colored("fastdevfs-cli start --mountpoint <path>", ui::Color::YELLOW) << "\n";
        std::cerr << "  Or set 'mountpoint' in " << g_config_path << "\n" << std::endl;
        exit(1);
    }

    // Validate mountpoint before proceeding
    if (!validate_mountpoint_cli(mountpoint)) {
        exit(1);
    }

    // Convert mountpoint to absolute path so the daemon can find it
    // regardless of CWD changes after daemonizing
    try {
        mountpoint = fs::canonical(mountpoint).string();
    } catch (const fs::filesystem_error& e) {
        ui::print_error("Cannot resolve mountpoint path: " + mountpoint);
        std::cerr << "  " << e.what() << "\n" << std::endl;
        exit(1);
    }

    // Locate daemon binary
    std::string daemon_bin = get_daemon_path();
    if (daemon_bin.empty()) {
        ui::print_error("Daemon binary 'fastdevfs' not found");
        std::cerr << "  Searched in:\n";
        std::cerr << "    - Same directory as fastdevfs-cli\n";
        std::cerr << "    - ./build/fastdevfs\n";
        std::cerr << "    - ./fastdevfs\n";
        std::cerr << "  Ensure the project is built: cd build && cmake .. && make\n" << std::endl;
        exit(1);
    }

    // Also resolve config path to absolute for the daemon
    std::string abs_config_path = g_config_path;
    try {
        abs_config_path = fs::canonical(g_config_path).string();
    } catch (...) {
        // Keep relative if it can't be resolved
    }

    std::ostringstream cmd;
    cmd << "\"" << daemon_bin << "\"";
    cmd << " \"" << mountpoint << "\"";

    if (!daemon_mode) {
        cmd << " -f";  // Foreground mode
    }

    // Pass config path via environment
    std::string env_config = "FASTDEVFS_CONFIG=\"" + abs_config_path + "\"";

    vlog("Starting daemon: " + cmd.str());
    
    ui::print_loading("Starting FastDevFS...");
    std::cout << "\r";
    ui::print_info("Mounting on: " + mountpoint);

    if (daemon_mode) {
        // Launch in background — use & so system() returns immediately
        std::string bg_cmd = env_config + " " + cmd.str() + " >/dev/null 2>&1 &";
        vlog("Executing: " + bg_cmd);
        int ret = system(bg_cmd.c_str());
        
        if (ret != 0) {
            ui::print_error("Failed to launch daemon process");
            std::cerr << "  Exit code: " << (ret >> 8) << std::endl;
            std::cerr << "  Check the mountpoint permissions and FUSE installation.\n" << std::endl;
            exit(1);
        }
        
        // Wait for daemon to initialize (IPC socket to appear)
        // Retry a few times to avoid race conditions
        bool started = false;
        for (int attempt = 0; attempt < 10; attempt++) {
            usleep(500000);  // 500ms per attempt
            if (is_daemon_running(config.socket_path)) {
                started = true;
                break;
            }
            vlog("Waiting for daemon... attempt " + std::to_string(attempt + 1));
        }
        
        if (!started) {
            ui::print_error("Daemon failed to start within 5 seconds");
            std::cerr << "  Possible causes:\n";
            std::cerr << "    - FUSE not installed (apt install fuse3)\n";
            std::cerr << "    - Permission denied (check user is in 'fuse' group)\n";
            std::cerr << "    - Mountpoint already in use (fusermount3 -u " << mountpoint << ")\n";
            std::cerr << "  Try foreground mode for detailed errors:\n";
            std::cerr << "    fastdevfs-cli start -m " << mountpoint << "\n" << std::endl;
            exit(1);
        }
        
        ui::print_success("Daemon started successfully in background");
        std::cout << "  Use '" << ui::colored("fastdevfs-cli status", ui::Color::YELLOW) 
                  << "' to check status\n" << std::endl;
    } else {
        // Foreground — exec replaces this process
        std::string fg_cmd = env_config + " " + cmd.str();
        vlog("Executing: " + fg_cmd);
        int ret = system(fg_cmd.c_str());
        exit(WEXITSTATUS(ret));
    }
}

// ============================================================
// Command: stop
// ============================================================

static void cmd_stop() {
    FastDevFsConfig config = load_config(g_config_path);

    ui::print_loading("Stopping FastDevFS...");
    std::cout << "\r";

    bool stopped = false;

    // Try IPC first
    if (is_daemon_running(config.socket_path)) {
        vlog("Sending stop command via IPC");
        std::string response = send_ipc_command(
            config.socket_path,
            ipc_make_request("stop")
        );

        std::string status = ipc_json_get(response, "status");
        if (status == "ok") {
            stopped = true;
        }
    }

    // Fallback: try PID file
    if (!stopped) {
        pid_t pid;
        if (read_pid_file(config.pid_path, pid)) {
            vlog("Sending SIGTERM to PID " + std::to_string(pid));
            if (kill(pid, SIGTERM) == 0) {
                stopped = true;
                vlog("PID: " + std::to_string(pid));
            }
        }
    }

    if (!stopped) {
        ui::print_error("FastDevFS daemon does not appear to be running");
        // Still clean up stale files
        cleanup_stale_state(config);
        exit(1);
    }

    // Wait briefly for shutdown to complete
    usleep(500000);

    // Clean up state files
    remove_pid_file(config.pid_path);
    if (fs::exists(config.socket_path)) {
        unlink(config.socket_path.c_str());
    }

    // Try to unmount if mountpoint was configured
    if (!config.mountpoint.empty() && fs::exists(config.mountpoint)) {
        std::string umount_cmd = "fusermount3 -u \"" + config.mountpoint + "\" 2>/dev/null";
        system(umount_cmd.c_str());
    }

    ui::print_success("FastDevFS daemon stopped");
}

// ============================================================
// Command: status
// ============================================================

static void cmd_status() {
    FastDevFsConfig config = load_config(g_config_path);

    if (!is_daemon_running(config.socket_path)) {
        ui::print_error("FastDevFS daemon is NOT running");

        // Check PID file for stale state
        pid_t pid;
        if (read_pid_file(config.pid_path, pid)) {
            std::cout << "  " << ui::symbol(ui::Symbol::BULLET) << "  Stale PID file found: " 
                      << ui::colored(std::to_string(pid), ui::Color::YELLOW) << std::endl;
        }
        exit(1);
    }

    // Query detailed status
    std::string response = send_ipc_command(
        config.socket_path,
        ipc_make_request("status")
    );

    std::string data = ipc_json_get(response, "data");

    ui::print_header("FastDevFS Status");

    // Parse pipe-delimited key=value pairs
    std::istringstream stream(data);
    std::string pair;
    while (std::getline(stream, pair, '|')) {
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string key = pair.substr(0, eq);
            std::string val = pair.substr(eq + 1);
            
            // Format key nicely
            std::replace(key.begin(), key.end(), '_', ' ');
            std::string formatted_key = key.substr(0, 1);
            std::transform(formatted_key.begin(), formatted_key.end(), 
                         formatted_key.begin(), ::toupper);
            formatted_key += key.substr(1);
            
            // Color-code specific values
            ui::Color val_color = ui::Color::RESET;
            if (val == "true" || val == "yes" || val == "running") {
                val_color = ui::Color::GREEN;
            } else if (val == "false" || val == "no" || val == "stopped") {
                val_color = ui::Color::RED;
            }
            
            std::cout << ui::format_kv_colored(formatted_key, val, val_color) << std::endl;
        }
    }
    std::cout << std::endl;
}

// ============================================================
// Command: scan
// ============================================================

static void cmd_scan(const std::string& path, bool recursive) {
    if (!fs::exists(path)) {
        ui::print_error("Path '" + path + "' does not exist");
        exit(1);
    }

    ui::print_header("Directory Scan");
    
    std::string scan_mode = recursive ? " (recursive)" : " (non-recursive)";
    ui::print_info("Scanning: " + ui::colored(path, ui::Color::YELLOW) + scan_mode);
    ui::print_divider(50);

    int file_count = 0;
    int dir_count = 0;
    uint64_t total_size = 0;

    auto process_entry = [&](const fs::directory_entry& entry) {
        if (entry.is_regular_file()) {
            file_count++;
            auto fsize = entry.file_size();
            total_size += fsize;

            if (g_verbose) {
                std::string hash = sha256_file(entry.path().string());
                std::ostringstream oss;
                oss << std::left << std::setw(60) << entry.path().string()
                    << "  " << std::right << std::setw(12) << fsize << " B"
                    << "  SHA256: " << hash.substr(0, 12) << "...";
                std::cout << oss.str() << std::endl;
            } else {
                std::cout << "  " << ui::symbol(ui::Symbol::FOLDER) << "  "
                          << std::left << std::setw(50) << entry.path().string()
                          << "  " << std::right << std::setw(10) << fsize << " B" << std::endl;
            }
        } else if (entry.is_directory()) {
            dir_count++;
        }
    };

    try {
        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(path)) {
                process_entry(entry);
            }
        } else {
            for (const auto& entry : fs::directory_iterator(path)) {
                process_entry(entry);
            }
        }
    } catch (const fs::filesystem_error& e) {
        ui::print_error(std::string(e.what()));
        exit(1);
    }

    ui::print_divider(50);
    
    std::cout << "\n  " << ui::colored("Summary", ui::Color::BOLD) << std::endl;
    std::cout << ui::format_kv("Files", std::to_string(file_count)) << std::endl;
    std::cout << ui::format_kv("Directories", std::to_string(dir_count)) << std::endl;
    
    // Format total size nicely
    std::ostringstream size_str;
    if (total_size < 1024) {
        size_str << total_size << " B";
    } else if (total_size < 1024 * 1024) {
        size_str << std::fixed << std::setprecision(2) << (total_size / 1024.0) << " KB";
    } else if (total_size < 1024 * 1024 * 1024) {
        size_str << std::fixed << std::setprecision(2) << (total_size / (1024.0 * 1024)) << " MB";
    } else {
        size_str << std::fixed << std::setprecision(2) << (total_size / (1024.0 * 1024 * 1024)) << " GB";
    }
    
    std::cout << ui::format_kv_colored("Total Size", size_str.str(), ui::Color::YELLOW) << std::endl;
    std::cout << std::endl;
}

// ============================================================
// Command: dedup run
// ============================================================

// ============================================================
// Helper: Parse JSON integers from IPC messages
// ============================================================

static int json_get_int(const std::string& json, const std::string& field) {
    // Find "field":number pattern
    std::string search = "\"" + field + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0;

    pos += search.size();
    size_t end = pos;
    while (end < json.size() && (std::isdigit(json[end]) || json[end] == '-')) {
        end++;
    }

    if (end > pos) {
        return std::stoi(json.substr(pos, end - pos));
    }
    return 0;
}

// ============================================================
// Command: dedup run (with real-time progress)
// ============================================================

static void cmd_dedup_run() {
    FastDevFsConfig config = load_config(g_config_path);

    if (!is_daemon_running(config.socket_path)) {
        ui::print_error("Daemon is not running");
        std::cerr << "  Deduplication requires the daemon to be active.\n";
        std::cerr << "  Start it with: " << ui::colored("fastdevfs-cli start", ui::Color::YELLOW) 
                  << "\n" << std::endl;
        exit(1);
    }

    vlog("Sending dedup_run via IPC (streaming mode)");
    
    // Open socket connection for streaming
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        ui::print_error("Failed to create socket");
        exit(1);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, config.socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        ui::print_error("Failed to connect to daemon");
        exit(1);
    }

    // Send dedup_run request
    std::string request = ipc_make_request("dedup_run");
    ssize_t sent = write(sock, request.c_str(), request.size());
    if (sent < 0) {
        close(sock);
        ui::print_error("Failed to send dedup request");
        exit(1);
    }

    ui::print_header("Deduplication Pass");
    std::cout << std::endl;

    // Read streaming responses
    char buf[4096];
    int last_percent = -1;
    bool pass_complete = false;
    int final_duplicates = 0;
    uint64_t final_saved = 0;

    while (!pass_complete) {
        memset(buf, 0, sizeof(buf));
        ssize_t n = read(sock, buf, sizeof(buf) - 1);
        
        if (n <= 0) {
            break;  // Connection closed
        }

        std::string msg(buf, n);
        vlog("Received IPC message: " + msg);

        // Parse message type
        std::string type = ipc_json_get(msg, "type");

        if (type == "dedup_progress") {
            int processed = json_get_int(msg, "processed");
            int total = json_get_int(msg, "total");
            int duplicates = json_get_int(msg, "duplicates");

            if (total > 0) {
                int percent = (processed * 100) / total;
                if (percent != last_percent) {
                    // Clear previous line and print new progress
                    ui::clear_line();
                    
                    // Progress bar style
                    std::cout << "  " << ui::colored(ui::symbol(ui::Symbol::HOURGLASS), ui::Color::YELLOW)
                              << "  Processing: " << ui::colored(std::to_string(processed), ui::Color::CYAN)
                              << " / " << std::to_string(total) << " files  ["
                              << percent << "%]" << std::flush;
                    
                    last_percent = percent;
                    
                    if (percent == 100) {
                        std::cout << "\n";
                    }
                }
            }

        } else if (type == "dedup_complete") {
            int processed = json_get_int(msg, "processed");
            int duplicates = json_get_int(msg, "duplicates");
            uint64_t saved_bytes = json_get_int(msg, "saved_bytes");
            
            final_duplicates = duplicates;
            final_saved = saved_bytes;
            pass_complete = true;

            // Clear any previous progress line
            ui::clear_line();
            
            // Print completion message
            std::cout << "\n";
            ui::print_success("Deduplication pass complete");
            std::cout << "\n  " << ui::colored("Summary", ui::Color::BOLD) << std::endl;
            std::cout << ui::format_kv("Files processed", std::to_string(processed)) << std::endl;
            std::cout << ui::format_kv_colored("Duplicates found", std::to_string(duplicates), 
                                               ui::Color::CYAN) << std::endl;
            
            // Format saved bytes nicely
            std::ostringstream size_str;
            if (saved_bytes < 1024) {
                size_str << saved_bytes << " B";
            } else if (saved_bytes < 1024 * 1024) {
                size_str << std::fixed << std::setprecision(2) << (saved_bytes / 1024.0) << " KB";
            } else if (saved_bytes < 1024 * 1024 * 1024) {
                size_str << std::fixed << std::setprecision(2) << (saved_bytes / (1024.0 * 1024)) << " MB";
            } else {
                size_str << std::fixed << std::setprecision(2) << (saved_bytes / (1024.0 * 1024 * 1024)) << " GB";
            }
            
            ui::Color saved_color = (saved_bytes > 0) ? ui::Color::GREEN : ui::Color::YELLOW;
            std::cout << ui::format_kv_colored("Space saved", size_str.str(), saved_color) << std::endl;
            std::cout << std::endl;
        }
    }

    close(sock);

    if (!pass_complete) {
        ui::print_warning("Dedup pass interrupted or incomplete");
        exit(1);
    }
}

// ============================================================
// Command: dedup stats
// ============================================================

static void cmd_dedup_stats() {
    FastDevFsConfig config = load_config(g_config_path);

    if (is_daemon_running(config.socket_path)) {
        std::string response = send_ipc_command(
            config.socket_path,
            ipc_make_request("dedup_stats")
        );
        std::string data = ipc_json_get(response, "data");

        ui::print_header("Deduplication Statistics");

        // Parse the response into config and dedup files
        std::string dedup_files_str;
        int total_duplicates = 0;
        uint64_t total_saved_bytes = 0;

        std::istringstream stream(data);
        std::string pair;
        while (std::getline(stream, pair, '|')) {
            size_t eq = pair.find('=');
            if (eq != std::string::npos) {
                std::string key = pair.substr(0, eq);
                std::string val = pair.substr(eq + 1);

                // Extract special fields
                if (key == "total_duplicates") {
                    total_duplicates = std::stoi(val);
                } else if (key == "total_saved_bytes") {
                    total_saved_bytes = std::stoull(val);
                } else if (key == "dedup_files") {
                    dedup_files_str = val;
                } else if (key != "dedup_files") {
                    // Display config fields
                    std::string display_key = key;
                    std::replace(display_key.begin(), display_key.end(), '_', ' ');
                    
                    // Capitalize first letter
                    if (!display_key.empty()) {
                        display_key[0] = std::toupper(display_key[0]);
                    }
                    
                    // Color-code specific keys
                    ui::Color val_color = ui::Color::RESET;
                    if (display_key.find("Enabled") != std::string::npos) {
                        val_color = (val == "true") ? ui::Color::GREEN : ui::Color::YELLOW;
                    } else if (display_key.find("Algorithm") != std::string::npos) {
                        val_color = ui::Color::CYAN;
                    }
                    
                    if (val_color != ui::Color::RESET) {
                        std::cout << ui::format_kv_colored(display_key, val, val_color) << std::endl;
                    } else {
                        std::cout << ui::format_kv(display_key, val) << std::endl;
                    }
                }
            }
        }

        std::cout << std::endl;

        // Display deduplicated files
        if (total_duplicates > 0) {
            ui::print_subheader("Deduplicated Files");
            std::cout << ui::format_kv("Total Duplicates", std::to_string(total_duplicates)) << std::endl;
            
            // Format saved bytes
            std::ostringstream size_str;
            if (total_saved_bytes < 1024) {
                size_str << total_saved_bytes << " B";
            } else if (total_saved_bytes < 1024 * 1024) {
                size_str << std::fixed << std::setprecision(2) << (total_saved_bytes / 1024.0) << " KB";
            } else if (total_saved_bytes < 1024 * 1024 * 1024) {
                size_str << std::fixed << std::setprecision(2) << (total_saved_bytes / (1024.0 * 1024)) << " MB";
            } else {
                size_str << std::fixed << std::setprecision(2) << (total_saved_bytes / (1024.0 * 1024 * 1024)) << " GB";
            }
            
            std::cout << ui::format_kv_colored("Total Space Saved", size_str.str(), ui::Color::GREEN)
                      << std::endl;

            std::cout << "\n  " << ui::colored("File Details:", ui::Color::BOLD) << std::endl;
            ui::print_divider(70);

            // Parse individual files
            std::istringstream file_stream(dedup_files_str);
            std::string file_entry;
            int file_num = 0;

            while (std::getline(file_stream, file_entry, '|')) {
                if (file_entry.empty()) continue;

                // Parse file_idx=X,refcount=Y,size=Z,hash=...
                int file_idx = 0;
                int refcount = 0;
                uint64_t size = 0;
                std::string hash;

                // Simple parsing
                std::istringstream entry_stream(file_entry);
                std::string token;
                while (std::getline(entry_stream, token, ',')) {
                    size_t colon = token.find('=');
                    if (colon != std::string::npos) {
                        std::string key = token.substr(0, colon);
                        std::string value = token.substr(colon + 1);

                        if (key == "file_idx") {
                            file_idx = std::stoi(value);
                        } else if (key == "refcount") {
                            refcount = std::stoi(value);
                        } else if (key == "size") {
                            size = std::stoull(value);
                        } else if (key == "hash") {
                            hash = value;
                        }
                    }
                }

                // Format size nicely
                std::ostringstream fsize_str;
                if (size < 1024) {
                    fsize_str << size << " B";
                } else if (size < 1024 * 1024) {
                    fsize_str << std::fixed << std::setprecision(1) << (size / 1024.0) << " KB";
                } else {
                    fsize_str << std::fixed << std::setprecision(1) << (size / (1024.0 * 1024)) << " MB";
                }

                file_num++;
                std::cout << "\n  " << ui::colored(std::to_string(file_num) + ".", ui::Color::CYAN);
                std::cout << " File Index: " << ui::colored(std::to_string(file_idx), ui::Color::BOLD);
                std::cout << " | Refcount: " << ui::colored(std::to_string(refcount), ui::Color::GREEN);
                std::cout << " | Size: " << fsize_str.str();
                std::cout << " | Hash: " << hash << std::endl;
            }

            ui::print_divider(70);
            std::cout << std::endl;
        } else {
            ui::print_info("No deduplicated files found");
            std::cout << std::endl;
        }
    } else {
        // Offline — show config-based info only
        FastDevFsConfig cfg = load_config(g_config_path);
        ui::print_header("Deduplication Statistics (Offline)");
        
        std::cout << ui::format_kv_colored("Dedup Enabled", cfg.dedup_enabled ? "yes" : "no", 
                                           cfg.dedup_enabled ? ui::Color::GREEN : ui::Color::YELLOW)
                  << std::endl;
        std::cout << ui::format_kv("Hash Algorithm", cfg.hash_algorithm) << std::endl;
        std::cout << ui::format_kv("Chunk Size", std::to_string(cfg.chunk_size) + " bytes") << std::endl;
        
        ui::print_info("Start daemon for live statistics");
        std::cout << std::endl;
    }
}

// ============================================================
// Command: hash
// ============================================================

static void cmd_hash(const std::string& filepath) {
    if (!fs::exists(filepath)) {
        ui::print_error("File '" + filepath + "' does not exist");
        exit(1);
    }

    if (!fs::is_regular_file(filepath)) {
        ui::print_error("'" + filepath + "' is not a regular file");
        exit(1);
    }

    vlog("Computing SHA-256 hash for: " + filepath);

    ui::print_loading("Computing hash...");
    std::cout << "\r";
    
    std::string hash = sha256_file(filepath);
    if (hash.empty()) {
        ui::print_error("Failed to compute hash for '" + filepath + "'");
        exit(1);
    }

    ui::print_success("Hash computed");
    std::cout << "\n  " << ui::colored(hash, ui::Color::YELLOW) << "\n  " 
              << ui::colored(filepath, ui::Color::BOLD) << "\n" << std::endl;
}

// ============================================================
// Command: config set
// ============================================================

static void cmd_config_set(const std::string& key, const std::string& value) {
    // Load, modify, save
    FastDevFsConfig config = load_config(g_config_path);

    if (!set_config_value(config, key, value)) {
        ui::print_error("Unknown config key '" + key + "'");
        std::cerr << "\n  Valid keys:\n";
        auto m = config_to_map(config);
        for (const auto& [k, v] : m) {
            std::cerr << "    " << k << "\n";
        }
        std::cerr << std::endl;
        exit(1);
    }

    if (!save_config(config, g_config_path)) {
        ui::print_error("Failed to write config to " + g_config_path);
        exit(1);
    }

    ui::print_success("Configuration updated");
    std::cout << ui::format_kv(key, value) << std::endl;

    // If daemon is running, propagate via IPC
    FastDevFsConfig check_cfg = load_config(g_config_path);
    if (is_daemon_running(check_cfg.socket_path)) {
        vlog("Propagating config update via IPC");
        std::string response = send_ipc_command(
            check_cfg.socket_path,
            ipc_make_request("config_update", key, value)
        );
        std::string status = ipc_json_get(response, "status");
        if (status == "ok") {
            ui::print_info("Applied to running daemon");
        } else {
            ui::print_warning("Config saved to disk but failed to propagate to daemon");
        }
    } else {
        vlog("Daemon not running — config saved to disk only");
    }
    std::cout << std::endl;
}

// ============================================================
// Command: config get
// ============================================================

static void cmd_config_get(const std::string& key) {
    FastDevFsConfig config = load_config(g_config_path);

    if (key.empty()) {
        // Show all config
        ui::print_header("FastDevFS Configuration");
        std::cout << ui::format_kv("Config Path", g_config_path) << "\n" << std::endl;
        
        auto m = config_to_map(config);
        for (const auto& [k, v] : m) {
            std::cout << ui::format_kv(k, v) << std::endl;
        }
        std::cout << std::endl;
        return;
    }

    // Try live value from daemon first
    if (is_daemon_running(config.socket_path)) {
        vlog("Querying live config from daemon");
        std::string response = send_ipc_command(
            config.socket_path,
            ipc_make_request("config_get", key)
        );
        std::string status = ipc_json_get(response, "status");
        std::string data   = ipc_json_get(response, "data");

        if (status == "ok") {
            ui::print_success("Config value (live from daemon)");
            std::cout << ui::format_kv_colored(key, data, ui::Color::YELLOW) << std::endl;
            return;
        }
    }

    // Fallback to file
    std::string value = get_config_value(config, key);
    if (value.empty()) {
        ui::print_error("Unknown config key '" + key + "'");
        exit(1);
    }

    ui::print_success("Config value (from file)");
    std::cout << ui::format_kv(key, value) << std::endl;
}

// ============================================================
// Command: library add
// ============================================================

static std::string g_tracked_libs_path = FASTDEVFS_DEFAULT_TRACKED_LIBS_PATH;

static void cmd_library_add(const std::string& name) {
    if (name.empty()) {
        ui::print_error("Library name cannot be empty");
        exit(1);
    }

    vlog("Adding library: " + name);

    if (add_tracked_library(name, g_tracked_libs_path)) {
        ui::print_success("Library added: " + ui::colored(name, ui::Color::CYAN));
    } else {
        ui::print_warning("Library already exists: " + ui::colored(name, ui::Color::YELLOW));
    }
    std::cout << std::endl;
}

// ============================================================
// Command: library check
// ============================================================

static void cmd_library_check(const std::string& name) {
    if (name.empty()) {
        ui::print_error("Library name cannot be empty");
        exit(1);
    }

    vlog("Checking library: " + name);

    if (is_tracked_library(name, g_tracked_libs_path)) {
        ui::print_success("Found in tracked libraries: " + ui::colored(name, ui::Color::GREEN));
    } else {
        ui::print_error("Not found in tracked libraries: " + ui::colored(name, ui::Color::YELLOW));
    }
    std::cout << std::endl;
}

// ============================================================
// Command: library list
// ============================================================

static void cmd_library_list() {
    auto libs = get_all_tracked_libraries(g_tracked_libs_path);

    ui::print_header("Tracked Libraries");
    std::cout << ui::format_kv("File", g_tracked_libs_path) << std::endl;
    std::cout << ui::format_kv_colored("Total", std::to_string(libs.size()), ui::Color::CYAN)
              << std::endl;
    ui::print_divider(50);

    if (libs.empty()) {
        ui::print_info("No libraries tracked yet");
        std::cout << "  Use: " << ui::colored("fastdevfs-cli library add <name>", ui::Color::YELLOW)
                  << std::endl;
    } else {
        // Print in columns for readability
        const int cols = 3;
        const int col_width = 28;
        for (size_t i = 0; i < libs.size(); i++) {
            if (i % cols == 0) {
                std::cout << "  ";
            }
            std::cout << std::left << std::setw(col_width) << libs[i];
            if ((i + 1) % cols == 0 || i + 1 == libs.size()) {
                std::cout << std::endl;
            }
        }
    }
    std::cout << std::endl;
}

// ============================================================
// Command: library remove
// ============================================================

static void cmd_library_remove(const std::string& name) {
    if (name.empty()) {
        ui::print_error("Library name cannot be empty");
        exit(1);
    }

    vlog("Removing library: " + name);

    if (remove_tracked_library(name, g_tracked_libs_path)) {
        ui::print_success("Library removed: " + ui::colored(name, ui::Color::CYAN));
    } else {
        ui::print_error("Library not found: " + ui::colored(name, ui::Color::YELLOW));
    }
    std::cout << std::endl;
}

// ============================================================
// Command: clean — remove stale PID/socket files
// ============================================================

static void cmd_clean() {
    FastDevFsConfig config = load_config(g_config_path);

    ui::print_header("Cleaning stale FastDevFS state");

    bool cleaned = false;

    // Check if daemon is actually running — refuse to clean if it is
    if (is_daemon_running(config.socket_path)) {
        ui::print_error("Daemon is still running! Stop it first with: fastdevfs-cli stop");
        exit(1);
    }

    // Remove stale PID file
    pid_t pid;
    if (read_pid_file(config.pid_path, pid)) {
        if (kill(pid, 0) != 0) {
            remove_pid_file(config.pid_path);
            ui::print_info("Removed stale PID file (pid " + std::to_string(pid) + " is dead)");
            cleaned = true;
        } else {
            ui::print_warning("PID " + std::to_string(pid) + " is still alive — sending SIGTERM");
            kill(pid, SIGTERM);
            usleep(500000);
            remove_pid_file(config.pid_path);
            cleaned = true;
        }
    }

    // Remove stale socket file
    if (fs::exists(config.socket_path)) {
        unlink(config.socket_path.c_str());
        ui::print_info("Removed stale socket: " + config.socket_path);
        cleaned = true;
    }

    // Try to unmount if mountpoint looks stale
    if (!config.mountpoint.empty()) {
        std::string umount_cmd = "fusermount3 -u \"" + config.mountpoint + "\" 2>/dev/null";
        int ret = system(umount_cmd.c_str());
        if (ret == 0) {
            ui::print_info("Unmounted stale FUSE mount: " + config.mountpoint);
            cleaned = true;
        }
    }

    if (cleaned) {
        ui::print_success("Cleanup complete");
    } else {
        ui::print_info("Nothing to clean — no stale state found");
    }
    std::cout << std::endl;
}

// ============================================================
// Main — CLI11 setup
// ============================================================

int main(int argc, char* argv[]) {
    CLI::App app{ui::colored("FastDevFS", ui::Color::YELLOW, ui::Color::BOLD) + 
                 " — Filesystem deduplication management tool"};
    app.require_subcommand(1);  // At least one subcommand required

    // Global flags
    app.add_flag("-v,--verbose", g_verbose, "Enable verbose output");
    app.add_option("--config", g_config_path,
                   "Config file path (default: ./config.ini)");

    // Initialize UI module
    app.callback([&]() {
        ui::set_verbose(g_verbose);
        // Detect Unicode support from environment
        if (std::getenv("LANG") && std::string(std::getenv("LANG")).find("UTF-8") == std::string::npos) {
            ui::set_unicode_support(false);
        }
    });

    // ── start ──────────────────────────────────────────────
    auto* start_cmd = app.add_subcommand("start", "Start the FastDevFS daemon");
    bool start_daemon = false;
    std::string start_mountpoint;
    start_cmd->add_flag("--daemon,-d", start_daemon,
                        "Run in background (daemon mode)");
    start_cmd->add_option("--mountpoint,-m", start_mountpoint,
                          "Mount point directory");
    start_cmd->callback([&]() {
        cmd_start(start_daemon, start_mountpoint);
    });

    // ── stop ───────────────────────────────────────────────
    auto* stop_cmd = app.add_subcommand("stop", "Stop the running FastDevFS daemon");
    stop_cmd->callback([&]() {
        cmd_stop();
    });

    // ── status ─────────────────────────────────────────────
    auto* status_cmd = app.add_subcommand("status", "Show daemon status");
    status_cmd->callback([&]() {
        cmd_status();
    });

    // ── clean ──────────────────────────────────────────────
    auto* clean_cmd = app.add_subcommand("clean", "Remove stale PID/socket files from a crashed daemon");
    clean_cmd->callback([&]() {
        cmd_clean();
    });

    // ── library ────────────────────────────────────────────
    auto* lib_cmd = app.add_subcommand("library", "Manage tracked library whitelist");
    lib_cmd->require_subcommand(1);

    auto* lib_add = lib_cmd->add_subcommand("add", "Add a library to the tracked list");
    std::string lib_add_name;
    lib_add->add_option("name", lib_add_name, "Library/package name")->required();
    lib_add->callback([&]() {
        cmd_library_add(lib_add_name);
    });

    auto* lib_check = lib_cmd->add_subcommand("check", "Check if a library is tracked");
    std::string lib_check_name;
    lib_check->add_option("name", lib_check_name, "Library/package name")->required();
    lib_check->callback([&]() {
        cmd_library_check(lib_check_name);
    });

    auto* lib_list = lib_cmd->add_subcommand("list", "List all tracked libraries");
    lib_list->callback([&]() {
        cmd_library_list();
    });

    auto* lib_remove = lib_cmd->add_subcommand("remove", "Remove a library from the tracked list");
    std::string lib_remove_name;
    lib_remove->add_option("name", lib_remove_name, "Library/package name")->required();
    lib_remove->callback([&]() {
        cmd_library_remove(lib_remove_name);
    });

    // ── scan ───────────────────────────────────────────────
    auto* scan_cmd = app.add_subcommand("scan", "Scan a directory and list files");
    std::string scan_path;
    bool scan_recursive = false;
    scan_cmd->add_option("path", scan_path, "Directory to scan")->required();
    scan_cmd->add_flag("-r,--recursive", scan_recursive, "Scan recursively");
    scan_cmd->callback([&]() {
        cmd_scan(scan_path, scan_recursive);
    });

    // ── dedup ──────────────────────────────────────────────
    auto* dedup_cmd = app.add_subcommand("dedup", "Deduplication operations");
    dedup_cmd->require_subcommand(1);

    auto* dedup_run = dedup_cmd->add_subcommand("run", "Trigger deduplication pass");
    dedup_run->callback([&]() {
        cmd_dedup_run();
    });

    auto* dedup_stats = dedup_cmd->add_subcommand("stats", "Show deduplication statistics");
    dedup_stats->callback([&]() {
        cmd_dedup_stats();
    });

    // ── hash ───────────────────────────────────────────────
    auto* hash_cmd = app.add_subcommand("hash", "Compute SHA-256 hash of a file");
    std::string hash_file;
    hash_cmd->add_option("file", hash_file, "File to hash")->required();
    hash_cmd->callback([&]() {
        cmd_hash(hash_file);
    });

    // ── config ─────────────────────────────────────────────
    auto* config_cmd = app.add_subcommand("config", "Configuration management");
    config_cmd->require_subcommand(1);

    // config set <key> <value>
    auto* config_set = config_cmd->add_subcommand("set", "Set a config value");
    std::string cfg_set_key, cfg_set_value;
    config_set->add_option("key", cfg_set_key, "Config key")->required();
    config_set->add_option("value", cfg_set_value, "Config value")->required();
    config_set->callback([&]() {
        cmd_config_set(cfg_set_key, cfg_set_value);
    });

    // config get [key]
    auto* config_get = config_cmd->add_subcommand("get", "Get a config value (or all)");
    std::string cfg_get_key;
    config_get->add_option("key", cfg_get_key, "Config key (omit for all)");
    config_get->callback([&]() {
        cmd_config_get(cfg_get_key);
    });

    // ── Parse and run ──────────────────────────────────────
    CLI11_PARSE(app, argc, argv);

    return 0;
}
