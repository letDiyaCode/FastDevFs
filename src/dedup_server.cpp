/*
 * Dedup Server — background worker with debounced hashing, hard-link dedup,
 *                and synchronous CoW break support.
 *
 * Architecture:
 *   - Worker thread:  listens on Unix socket, manages debounce timers
 *   - Timer logic:    integrated into worker event loop via poll() timeout
 *   - CoW/delete:     called synchronously from FUSE thread (thread-safe via DedupIndex mutex)
 */

#include "../include/dedup_server.h"
#include "../include/dedup_ipc.h"
#include "../include/sha256.h"

#include <iostream>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <cstring>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

// ============================================================
// Constants
// ============================================================

#define FASTDEVFS_DATA_DIR "/tmp/fastdevfs_data"
static const int DEBOUNCE_MS = 500;       // Debounce delay in milliseconds
static const int POLL_TIMEOUT_MS = 100;   // How often to check timers

// ============================================================
// Globals
// ============================================================

static DedupIndex       g_index;
static std::atomic<bool> g_running{false};
static std::thread       g_worker_thread;

// ============================================================
// Helpers
// ============================================================

static std::string host_data_path(int index) {
    return std::string(FASTDEVFS_DATA_DIR) + "/" + std::to_string(index);
}

static int ino_to_index(uint64_t ino) {
    return (int)(ino - 1);
}

// ============================================================
// Debounce state (worker-thread-local, no mutex needed)
// ============================================================

struct PendingDedup {
    uint64_t inode;
    int      file_index;
    std::string path;
    bool     is_library;
    std::chrono::steady_clock::time_point fire_time;
};

static std::unordered_map<uint64_t, PendingDedup> g_pending;

// ============================================================
// Core dedup logic (called when debounce timer expires)
// ============================================================

static void process_dedup(const PendingDedup& pd) {
    std::string data_path = host_data_path(pd.file_index);

    // Check if file exists and is non-empty
    struct stat st;
    if (stat(data_path.c_str(), &st) != 0 || st.st_size == 0) {
        // Empty or missing — register in reverse map but skip dedup
        return;
    }

    // Compute SHA-256 (streaming, constant memory)
    std::string new_hash = sha256_file(data_path);
    if (new_hash.empty()) {
        std::cerr << "[Dedup] Failed to hash " << data_path << std::endl;
        return;
    }

    // Lock the index for the entire operation
    g_index.lock();

    // Check old hash for this inode (update case)
    std::string old_hash = g_index.get_inode_hash(pd.file_index);
    bool old_is_lib = g_index.get_inode_is_library(pd.file_index);

    if (!old_hash.empty() && old_hash == new_hash) {
        // Content unchanged — nothing to do
        g_index.unlock();
        return;
    }

    // If there was an old hash, decrement its refcount
    if (!old_hash.empty()) {
        DedupEntry* old_entry = g_index.lookup(old_hash.c_str(), old_is_lib);
        if (old_entry) {
            old_entry->refcount--;
            if (old_entry->refcount <= 0) {
                // No one uses this content anymore
                old_entry->occupied = false;
                old_entry->content_hash[0] = '\0';
                old_entry->refcount = 0;
                old_entry->canonical_index = -1;
            } else if (old_entry->canonical_index == pd.file_index) {
                // This was the canonical — reassign
                int other = g_index.find_other_inode_with_hash(old_hash.c_str(),
                                                                pd.file_index);
                if (other >= 0) {
                    old_entry->canonical_index = other;
                }
            }
        }
        g_index.remove_inode(pd.file_index);
    }

    // Look up new hash (with matching is_library to prevent cross-linking)
    DedupEntry* entry = g_index.lookup(new_hash.c_str(), pd.is_library);

    if (entry) {
        // DUPLICATE — hard-link to canonical copy
        std::string canonical_path = host_data_path(entry->canonical_index);

        // Verify canonical file exists
        struct stat can_st;
        if (stat(canonical_path.c_str(), &can_st) == 0) {
            // Remove current file and hard-link to canonical
            if (unlink(data_path.c_str()) == 0 &&
                link(canonical_path.c_str(), data_path.c_str()) == 0) {
                entry->refcount++;
                g_index.set_inode_hash(pd.file_index, new_hash.c_str(),
                                       pd.is_library);
                std::cout << "[Dedup] Linked " << data_path
                          << " → " << canonical_path
                          << " (refcount=" << entry->refcount << ")"
                          << std::endl;
            } else {
                // Hard-link failed — keep as independent file
                std::cerr << "[Dedup] Hard-link failed for " << data_path
                          << std::endl;
                g_index.insert(new_hash.c_str(), pd.file_index, pd.is_library);
                g_index.set_inode_hash(pd.file_index, new_hash.c_str(),
                                       pd.is_library);
            }
        } else {
            // Canonical file gone — this file becomes the new canonical
            entry->canonical_index = pd.file_index;
            entry->refcount++;
            g_index.set_inode_hash(pd.file_index, new_hash.c_str(),
                                   pd.is_library);
        }
    } else {
        // UNIQUE — register as new canonical
        g_index.insert(new_hash.c_str(), pd.file_index, pd.is_library);
        g_index.set_inode_hash(pd.file_index, new_hash.c_str(), pd.is_library);
        std::cout << "[Dedup] Registered canonical " << data_path
                  << " hash=" << new_hash.substr(0, 12) << "..."
                  << std::endl;
    }

    g_index.unlock();
}

// ============================================================
// Process expired debounce timers
// ============================================================

static void process_expired_timers() {
    auto now = std::chrono::steady_clock::now();
    std::vector<uint64_t> expired;

    for (auto& [inode, pd] : g_pending) {
        if (now >= pd.fire_time) {
            expired.push_back(inode);
        }
    }

    for (uint64_t inode : expired) {
        PendingDedup pd = std::move(g_pending[inode]);
        g_pending.erase(inode);
        process_dedup(pd);
    }
}

// ============================================================
// Handle incoming DedupRequest
// ============================================================

static void handle_request(const DedupRequest& req) {
    int file_index = ino_to_index(req.inode);

    if (req.operation_type == 3) {
        // DELETE — process immediately (handled synchronously in dedup_notify_delete)
        // This shouldn't arrive via socket if we call dedup_notify_delete directly,
        // but handle it for completeness.
        dedup_notify_delete(file_index);
        return;
    }

    // Policy check (defense in depth — sender should have already filtered)
    if (!should_dedup(req.is_library)) {
        return;
    }

    // For insert (op=1) on empty files, skip
    if (req.operation_type == 1) {
        std::string data_path = host_data_path(file_index);
        struct stat st;
        if (stat(data_path.c_str(), &st) != 0 || st.st_size == 0) {
            return;
        }
    }

    // Set/reset debounce timer
    PendingDedup pd;
    pd.inode = req.inode;
    pd.file_index = file_index;
    pd.path = req.path;
    pd.is_library = req.is_library;
    pd.fire_time = std::chrono::steady_clock::now()
                   + std::chrono::milliseconds(DEBOUNCE_MS);

    g_pending[req.inode] = std::move(pd);
}

// ============================================================
// Worker thread main loop
// ============================================================

static void dedup_worker() {
    int server_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (server_fd < 0) {
        std::cerr << "[Dedup] Failed to create socket" << std::endl;
        return;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DEDUP_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    unlink(DEDUP_SOCKET_PATH);
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[Dedup] Failed to bind socket" << std::endl;
        close(server_fd);
        return;
    }

    std::cout << "[Dedup] Worker started, listening on "
              << DEDUP_SOCKET_PATH << std::endl;

    struct pollfd pfd;
    pfd.fd = server_fd;
    pfd.events = POLLIN;

    while (g_running.load()) {
        int ret = poll(&pfd, 1, POLL_TIMEOUT_MS);

        if (ret > 0 && (pfd.revents & POLLIN)) {
            DedupRequest req;
            ssize_t n = recvfrom(server_fd, &req, sizeof(req), 0,
                                 nullptr, nullptr);
            if (n == sizeof(DedupRequest)) {
                handle_request(req);
            }
        }

        // Process expired debounce timers
        process_expired_timers();
    }

    // Process any remaining pending timers before shutdown
    for (auto& [inode, pd] : g_pending) {
        process_dedup(pd);
    }
    g_pending.clear();

    close(server_fd);
    unlink(DEDUP_SOCKET_PATH);
    std::cout << "[Dedup] Worker stopped" << std::endl;
}

// ============================================================
// Public API
// ============================================================

void start_dedup_server(const char* dedup_mmap_path) {
    if (!g_index.init(dedup_mmap_path)) {
        std::cerr << "[Dedup] Failed to init dedup index at "
                  << dedup_mmap_path << std::endl;
        return;
    }

    // Load policy from environment variable
    const char* env_policy = getenv("FASTDEVFS_DEDUP_POLICY");
    if (env_policy) {
        if (strcmp(env_policy, "user") == 0 || strcmp(env_policy, "user_only") == 0) {
            g_index.set_policy(DedupPolicy::DEDUP_USER_ONLY);
            std::cout << "[Dedup] Policy: USER_ONLY" << std::endl;
        } else if (strcmp(env_policy, "library") == 0 || strcmp(env_policy, "library_only") == 0) {
            g_index.set_policy(DedupPolicy::DEDUP_LIBRARY_ONLY);
            std::cout << "[Dedup] Policy: LIBRARY_ONLY" << std::endl;
        } else if (strcmp(env_policy, "none") == 0) {
            g_index.set_policy(DedupPolicy::DEDUP_NONE);
            std::cout << "[Dedup] Policy: NONE (disabled)" << std::endl;
        } else {
            g_index.set_policy(DedupPolicy::DEDUP_ALL);
            std::cout << "[Dedup] Policy: ALL" << std::endl;
        }
    } else {
        std::cout << "[Dedup] Policy: ALL (default)" << std::endl;
    }

    g_running.store(true);
    g_worker_thread = std::thread(dedup_worker);
}

void stop_dedup_server() {
    g_running.store(false);
    if (g_worker_thread.joinable()) {
        g_worker_thread.join();
    }
    g_index.close();
}

bool is_file_shared(uint64_t inode) {
    int idx = ino_to_index(inode);
    return g_index.is_shared(idx);
}

int dedup_cow_break(int file_index, int& fd) {
    std::string data_path = host_data_path(file_index);

    g_index.lock();

    // Get the current hash for this inode
    std::string hash = g_index.get_inode_hash(file_index);
    bool is_lib = g_index.get_inode_is_library(file_index);
    if (hash.empty()) {
        g_index.unlock();
        return 0;  // No dedup state — nothing to break
    }

    // Check if actually shared
    DedupEntry* entry = g_index.lookup(hash.c_str(), is_lib);
    if (!entry || entry->refcount <= 1) {
        g_index.unlock();
        return 0;  // Not shared — nothing to break
    }

    // We need to break the hard link.
    // 1. Read current data via the open fd
    struct stat st;
    if (fstat(fd, &st) < 0) {
        g_index.unlock();
        return -1;
    }

    size_t file_size = (size_t)st.st_size;
    std::vector<char> buf;

    if (file_size > 0) {
        buf.resize(file_size);
        ssize_t nread = pread(fd, buf.data(), file_size, 0);
        if (nread < 0) {
            g_index.unlock();
            return -1;
        }
        file_size = (size_t)nread;
    }

    // 2. Create temp file with the copied data
    std::string temp_path = data_path + ".cow_tmp";
    int tmp_fd = open(temp_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (tmp_fd < 0) {
        g_index.unlock();
        return -1;
    }

    if (file_size > 0) {
        ssize_t nwritten = write(tmp_fd, buf.data(), file_size);
        if (nwritten < 0 || (size_t)nwritten != file_size) {
            close(tmp_fd);
            unlink(temp_path.c_str());
            g_index.unlock();
            return -1;
        }
    }
    close(tmp_fd);

    // 3. Close old fd, replace hard-link with private copy
    close(fd);
    unlink(data_path.c_str());
    if (rename(temp_path.c_str(), data_path.c_str()) != 0) {
        // Failed — try to recover
        g_index.unlock();
        fd = open(data_path.c_str(), O_RDWR | O_CREAT, 0644);
        return -1;
    }

    // 4. Reopen the file
    fd = open(data_path.c_str(), O_RDWR, 0644);
    if (fd < 0) {
        g_index.unlock();
        return -1;
    }

    // 5. Update dedup state
    entry->refcount--;
    if (entry->refcount <= 0) {
        entry->occupied = false;
        entry->content_hash[0] = '\0';
        entry->refcount = 0;
        entry->canonical_index = -1;
    } else if (entry->canonical_index == file_index) {
        // Reassign canonical
        int other = g_index.find_other_inode_with_hash(hash.c_str(), file_index);
        if (other >= 0) {
            entry->canonical_index = other;
        }
    }

    // Remove this inode from reverse map (it's now independent)
    g_index.remove_inode(file_index);

    g_index.unlock();

    std::cout << "[Dedup] CoW break for index=" << file_index << std::endl;
    return 0;
}

void dedup_notify_delete(int file_index) {
    g_index.lock();

    std::string hash = g_index.get_inode_hash(file_index);
    bool is_lib = g_index.get_inode_is_library(file_index);

    if (hash.empty()) {
        g_index.unlock();
        return;
    }

    DedupEntry* entry = g_index.lookup(hash.c_str(), is_lib);
    if (entry) {
        entry->refcount--;
        if (entry->refcount <= 0) {
            entry->occupied = false;
            entry->content_hash[0] = '\0';
            entry->refcount = 0;
            entry->canonical_index = -1;
        } else if (entry->canonical_index == file_index) {
            // Reassign canonical to another inode sharing this content
            int other = g_index.find_other_inode_with_hash(hash.c_str(),
                                                            file_index);
            if (other >= 0) {
                entry->canonical_index = other;
            }
        }
    }

    g_index.remove_inode(file_index);
    g_index.unlock();
}

void set_dedup_policy(DedupPolicy policy) {
    g_index.set_policy(policy);
}

DedupPolicy get_dedup_policy() {
    return g_index.get_policy();
}

bool should_dedup(bool is_library) {
    DedupPolicy p = g_index.get_policy();
    switch (p) {
        case DedupPolicy::DEDUP_NONE:
            return false;
        case DedupPolicy::DEDUP_USER_ONLY:
            return !is_library;
        case DedupPolicy::DEDUP_LIBRARY_ONLY:
            return is_library;
        case DedupPolicy::DEDUP_ALL:
        default:
            return true;
    }
}
