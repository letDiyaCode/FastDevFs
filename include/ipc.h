#ifndef FASTDEVFS_IPC_H
#define FASTDEVFS_IPC_H

/*
 * FastDevFs IPC System
 *
 * Unix domain socket-based IPC for CLI ↔ Daemon communication.
 * Uses a separate socket from the dedup IPC channel.
 *
 * Protocol: newline-delimited JSON strings.
 * Request:  {"type":"<cmd>", "key":"<k>", "value":"<v>"}
 * Response: {"status":"ok|error", "data":"<result>"}
 */

#include <string>
#include <functional>

// ---- Daemon-side (server) ----

// Start IPC server thread. Must be called after config is loaded.
// socket_path: Unix socket file path (from config).
void start_ipc_server(const std::string& socket_path);

// Stop IPC server and clean up socket file.
void stop_ipc_server();

// Check if the IPC server is currently running.
bool ipc_server_running();

// ---- CLI-side (client) ----

// Send a JSON command to the running daemon via socket.
// Returns the response string, or empty string on connection failure.
std::string send_ipc_command(const std::string& socket_path,
                             const std::string& json_message);

// Check if the daemon is running and responsive.
bool is_daemon_running(const std::string& socket_path);

// ---- JSON helpers (minimal, no external dependency) ----

// Build a JSON request string.
std::string ipc_make_request(const std::string& type,
                             const std::string& key = "",
                             const std::string& value = "");

// Build a JSON response string.
std::string ipc_make_response(const std::string& status,
                              const std::string& data = "");

// Extract a field value from a simple JSON string.
// Only handles flat {"key":"value",...} format.
std::string ipc_json_get(const std::string& json, const std::string& field);

// ---- PID file management ----

bool write_pid_file(const std::string& path);
bool read_pid_file(const std::string& path, pid_t& pid);
void remove_pid_file(const std::string& path);

#endif /* FASTDEVFS_IPC_H */
