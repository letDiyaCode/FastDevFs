/*
 * FastDevFs IPC System — Unix domain socket server/client implementation.
 *
 * Server: runs in a background thread inside the daemon process.
 *         Accepts connections, reads JSON commands, dispatches, responds.
 *
 * Client: connects to socket, sends command, reads response, disconnects.
 */

#include "../include/ipc.h"
#include "../include/config.h"
#include "../include/dedup_server.h"

#include <iostream>
#include <thread>
#include <atomic>
#include <cstring>
#include <sstream>
#include <fstream>
#include <vector>
#include <chrono>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>

// ============================================================
// Constants
// ============================================================

static const int IPC_BACKLOG       = 5;
static const int IPC_BUF_SIZE      = 4096;
static const int IPC_POLL_TIMEOUT  = 200;   // ms — how often to check shutdown flag
static const int IPC_CLIENT_TIMEOUT = 3000; // ms — client read timeout

// ============================================================
// Server state
// ============================================================

static std::atomic<bool> g_ipc_running{false};
static std::thread       g_ipc_thread;
static std::string       g_ipc_socket_path;

// ============================================================
// Minimal JSON helpers
// ============================================================

// Simple JSON string escaping (handles quotes and backslashes)
static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"')       out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    return out;
}

// Simple JSON string unescaping
static std::string json_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[i + 1];
            if (next == '"')       { out += '"'; i++; }
            else if (next == '\\') { out += '\\'; i++; }
            else if (next == 'n')  { out += '\n'; i++; }
            else if (next == 'r')  { out += '\r'; i++; }
            else if (next == 't')  { out += '\t'; i++; }
            else                   { out += s[i]; }
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string ipc_make_request(const std::string& type,
                             const std::string& key,
                             const std::string& value) {
    std::ostringstream ss;
    ss << "{\"type\":\"" << json_escape(type) << "\"";
    if (!key.empty()) {
        ss << ",\"key\":\"" << json_escape(key) << "\"";
    }
    if (!value.empty()) {
        ss << ",\"value\":\"" << json_escape(value) << "\"";
    }
    ss << "}";
    return ss.str();
}

std::string ipc_make_response(const std::string& status,
                              const std::string& data) {
    std::ostringstream ss;
    ss << "{\"status\":\"" << json_escape(status) << "\"";
    if (!data.empty()) {
        ss << ",\"data\":\"" << json_escape(data) << "\"";
    }
    ss << "}";
    return ss.str();
}

std::string ipc_json_get(const std::string& json, const std::string& field) {
    // Find "field":"value" pattern
    std::string search = "\"" + field + "\":\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos += search.size();
    size_t end = pos;

    // Find closing quote, respecting escapes
    while (end < json.size()) {
        if (json[end] == '\\' && end + 1 < json.size()) {
            end += 2;  // Skip escaped char
        } else if (json[end] == '"') {
            break;
        } else {
            end++;
        }
    }

    if (end > json.size()) return "";
    return json_unescape(json.substr(pos, end - pos));
}

// ============================================================
// PID file management
// ============================================================

bool write_pid_file(const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return false;
    f << getpid();
    f.close();
    return true;
}

bool read_pid_file(const std::string& path, pid_t& pid) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    f >> pid;
    return !f.fail();
}

void remove_pid_file(const std::string& path) {
    unlink(path.c_str());
}

// ============================================================
// Server: handle a single client connection
// ============================================================

static void handle_ipc_client(int client_fd) {
    char buf[IPC_BUF_SIZE];
    memset(buf, 0, sizeof(buf));

    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        close(client_fd);
        return;
    }

    std::string request(buf, n);
    std::string type  = ipc_json_get(request, "type");
    std::string key   = ipc_json_get(request, "key");
    std::string value = ipc_json_get(request, "value");

    std::string response;

    if (type == "ping") {
        // Health check
        response = ipc_make_response("ok", "pong");

    } else if (type == "status") {
        // Return running status with basic info
        FastDevFsConfig cfg = get_global_config();
        std::ostringstream info;
        info << "running=true"
             << "|pid=" << getpid()
             << "|mountpoint=" << cfg.mountpoint
             << "|dedup_enabled=" << (cfg.dedup_enabled ? "true" : "false")
             << "|log_level=" << cfg.log_level
             << "|max_threads=" << cfg.max_threads;
        response = ipc_make_response("ok", info.str());

    } else if (type == "config_get") {
        if (key.empty()) {
            // Return all config as key=value pairs
            FastDevFsConfig cfg = get_global_config();
            auto m = config_to_map(cfg);
            std::ostringstream info;
            bool first = true;
            for (const auto& [k, v] : m) {
                if (!first) info << "|";
                info << k << "=" << v;
                first = false;
            }
            response = ipc_make_response("ok", info.str());
        } else {
            FastDevFsConfig cfg = get_global_config();
            std::string val = get_config_value(cfg, key);
            if (val.empty()) {
                response = ipc_make_response("error", "unknown key: " + key);
            } else {
                response = ipc_make_response("ok", val);
            }
        }

    } else if (type == "config_update") {
        if (key.empty()) {
            response = ipc_make_response("error", "missing key");
        } else if (!update_global_config(key, value)) {
            response = ipc_make_response("error", "unknown key: " + key);
        } else {
            // Also persist to disk
            FastDevFsConfig cfg = get_global_config();
            std::string config_path = FASTDEVFS_DEFAULT_CONFIG_PATH;
            save_config(cfg, config_path);
            response = ipc_make_response("ok", key + " = " + value);
        }

    } else if (type == "stop") {
        response = ipc_make_response("ok", "shutting down");
        // Write response before triggering shutdown
        write(client_fd, response.c_str(), response.size());
        close(client_fd);
        // Signal the process to stop (FUSE handles SIGTERM gracefully)
        kill(getpid(), SIGTERM);
        return;

    } else if (type == "dedup_run") {
        // Trigger full dedup pass and stream progress updates
        trigger_dedup_pass();

        // Send progress updates until pass completes
        int last_processed = -1;
        while (true) {
            int processed, total, duplicates;
            uint64_t saved_bytes;
            bool running = get_dedup_pass_progress(processed, total, duplicates, saved_bytes);

            // Send progress update if changed
            if (processed != last_processed) {
                std::ostringstream prog_msg;
                prog_msg << "{\"type\":\"dedup_progress\""
                         << ",\"processed\":" << processed
                         << ",\"total\":" << total
                         << ",\"duplicates\":" << duplicates
                         << ",\"saved_bytes\":" << saved_bytes << "}";
                std::string prog_str = prog_msg.str();
                write(client_fd, prog_str.c_str(), prog_str.size());
                last_processed = processed;
            }

            if (!running) {
                // Pass is complete — send final message
                std::ostringstream final_msg;
                final_msg << "{\"type\":\"dedup_complete\""
                          << ",\"processed\":" << processed
                          << ",\"duplicates\":" << duplicates
                          << ",\"saved_bytes\":" << saved_bytes << "}";
                std::string final_str = final_msg.str();
                write(client_fd, final_str.c_str(), final_str.size());
                break;
            }

            // Sleep briefly before next update
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        close(client_fd);
        return;


    } else if (type == "dedup_stats") {
        // Stats from config + actual deduplicated files
        FastDevFsConfig cfg = get_global_config();
        int total_duplicates = 0;
        uint64_t total_saved_bytes = 0;
        std::string dedup_files = get_deduplicated_files_info(total_duplicates, total_saved_bytes);

        std::ostringstream info;
        info << "dedup_enabled=" << (cfg.dedup_enabled ? "true" : "false")
             << "|hash_algorithm=" << cfg.hash_algorithm
             << "|chunk_size=" << cfg.chunk_size
             << "|total_duplicates=" << total_duplicates
             << "|total_saved_bytes=" << total_saved_bytes
             << "|dedup_files=" << json_escape(dedup_files);
        response = ipc_make_response("ok", info.str());

    } else {
        response = ipc_make_response("error", "unknown command: " + type);
    }

    // Send response
    write(client_fd, response.c_str(), response.size());
    close(client_fd);
}

// ============================================================
// Server: main accept loop
// ============================================================

static void ipc_server_loop() {
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[IPC] Failed to create socket: " << strerror(errno) << std::endl;
        return;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_ipc_socket_path.c_str(), sizeof(addr.sun_path) - 1);

    // Remove stale socket file
    unlink(g_ipc_socket_path.c_str());

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[IPC] Failed to bind socket: " << strerror(errno) << std::endl;
        close(server_fd);
        return;
    }

    if (listen(server_fd, IPC_BACKLOG) < 0) {
        std::cerr << "[IPC] Failed to listen: " << strerror(errno) << std::endl;
        close(server_fd);
        return;
    }

    // Set non-blocking so we can poll with timeout
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    std::cout << "[IPC] Server started on " << g_ipc_socket_path << std::endl;

    struct pollfd pfd;
    pfd.fd = server_fd;
    pfd.events = POLLIN;

    while (g_ipc_running.load()) {
        int ret = poll(&pfd, 1, IPC_POLL_TIMEOUT);

        if (ret > 0 && (pfd.revents & POLLIN)) {
            int client_fd = accept(server_fd, nullptr, nullptr);
            if (client_fd >= 0) {
                // Handle client synchronously (commands are fast)
                handle_ipc_client(client_fd);
            }
        }
    }

    close(server_fd);
    unlink(g_ipc_socket_path.c_str());
    std::cout << "[IPC] Server stopped" << std::endl;
}

// ============================================================
// Public API — Server
// ============================================================

void start_ipc_server(const std::string& socket_path) {
    if (g_ipc_running.load()) {
        std::cerr << "[IPC] Server already running" << std::endl;
        return;
    }

    g_ipc_socket_path = socket_path;
    g_ipc_running.store(true);
    g_ipc_thread = std::thread(ipc_server_loop);
}

void stop_ipc_server() {
    g_ipc_running.store(false);
    if (g_ipc_thread.joinable()) {
        g_ipc_thread.join();
    }
}

bool ipc_server_running() {
    return g_ipc_running.load();
}

// ============================================================
// Public API — Client
// ============================================================

std::string send_ipc_command(const std::string& socket_path,
                             const std::string& json_message) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        return "";
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return "";
    }

    // Send request
    ssize_t sent = write(sock, json_message.c_str(), json_message.size());
    if (sent < 0) {
        close(sock);
        return "";
    }

    // Read response with timeout
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, IPC_CLIENT_TIMEOUT);
    if (ret <= 0) {
        close(sock);
        return "";
    }

    char buf[IPC_BUF_SIZE];
    memset(buf, 0, sizeof(buf));
    ssize_t n = read(sock, buf, sizeof(buf) - 1);
    close(sock);

    if (n <= 0) return "";
    return std::string(buf, n);
}

bool is_daemon_running(const std::string& socket_path) {
    // Quick check: does the socket file exist?
    struct stat st;
    if (stat(socket_path.c_str(), &st) != 0) {
        return false;
    }

    // Try sending a ping
    std::string response = send_ipc_command(socket_path,
                                            ipc_make_request("ping"));
    if (response.empty()) return false;

    std::string status = ipc_json_get(response, "status");
    return (status == "ok");
}
