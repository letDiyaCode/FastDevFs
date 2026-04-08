#define FUSE_USE_VERSION 31

#include <fuse3/fuse_lowlevel.h>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include "../include/daemon/directory tree/adt.h"
#include "../include/daemon/fuse_lowlevel_ops.h"
#include "../include/dedup_server.h"
#include "../include/library_dedup.h"
#include "../include/config.h"
#include "../include/ipc.h"

#define FASTDEVFS_PERSIST_PATH "/tmp/fastdevfs.mmap"
#define FASTDEVFS_DEDUP_PATH  "/tmp/fastdevfs_dedup.mmap"
#define FASTDEVFS_LIB_CONFIG  "/tmp/fastdevfs_lib_config.txt"

// Global treefile pointer (mmap'd) and mmap state
treefile* file1 = nullptr;
static int persist_fd = -1;
static size_t persist_mapsize = 0;

// ============================================================
// Mountpoint Validation Helper
// ============================================================

/**
 * Validate that a mountpoint is suitable for FUSE mounting.
 * 
 * Returns: true if valid, false otherwise
 * Prints detailed error messages on failure.
 */
static bool validate_mountpoint(const char* path) {
    if (!path || strlen(path) == 0) {
        std::cerr << "Error: mountpoint path is empty." << std::endl;
        return false;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        int err = errno;
        std::cerr << "Error: mountpoint '" << path << "' does not exist." << std::endl;
        std::cerr << "       Create it with: mkdir -p " << path << std::endl;
        return false;
    }

    // Check if it's a directory
    if (!S_ISDIR(st.st_mode)) {
        std::cerr << "Error: mountpoint '" << path << "' is not a directory." << std::endl;
        std::cerr << "       Please provide a directory path, not a file." << std::endl;
        return false;
    }

    // Check read/write/execute permissions
    if (access(path, R_OK | W_OK | X_OK) != 0) {
        int err = errno;
        std::cerr << "Error: insufficient permissions for mountpoint '" << path << "'." << std::endl;
        
        if (err == EACCES) {
            // Check individual permissions
            if (access(path, R_OK) != 0) {
                std::cerr << "       Missing: read permission" << std::endl;
            }
            if (access(path, W_OK) != 0) {
                std::cerr << "       Missing: write permission" << std::endl;
            }
            if (access(path, X_OK) != 0) {
                std::cerr << "       Missing: execute permission" << std::endl;
            }
            std::cerr << "       Fix with: chmod u+rwx " << path << std::endl;
        } else {
            std::cerr << "       Reason: " << strerror(err) << std::endl;
        }
        return false;
    }

    // Check if directory is empty or only contains '/' entries
    DIR* dir = opendir(path);
    if (!dir) {
        int err = errno;
        std::cerr << "Error: cannot open mountpoint '" << path << "': " 
                  << strerror(err) << std::endl;
        return false;
    }

    int entry_count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Skip '.' and '..'
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        entry_count++;
        break;  // We just need to know if there's anything other than . and ..
    }
    closedir(dir);

    if (entry_count > 0) {
        std::cerr << "Warning: mountpoint '" << path << "' is not empty." << std::endl;
        std::cerr << "         FUSE typically requires an empty directory." << std::endl;
        std::cerr << "         This may cause mounting to fail. Continue anyway? [y/N] ";
        // In daemon mode, we can't prompt, so we'll allow it but warn
        std::cerr << "(proceeding in daemon mode)" << std::endl;
    }

    return true;
}

int main(int argc, char *argv[]) {
    // ── Load configuration ──────────────────────────────────
    // Check for config path from environment (set by CLI tool)
    std::string config_path = FASTDEVFS_DEFAULT_CONFIG_PATH;
    const char* env_config = getenv("FASTDEVFS_CONFIG");
    if (env_config && env_config[0] != '\0') {
        config_path = env_config;
    }

    FastDevFsConfig config = load_config(config_path);
    set_global_config(config);

    // Use config values for paths
    std::string persist_path = config.persist_path;
    std::string dedup_path   = config.dedup_path;

    // ── Parse FUSE command line ─────────────────────────────
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct fuse_cmdline_opts opts;
    memset(&opts, 0, sizeof(opts));

    if (fuse_parse_cmdline(&args, &opts) != 0) {
        return 1;
    }

    if (opts.show_help) {
        std::cout << "usage: " << argv[0] << " [options] <mountpoint>" << std::endl;
        fuse_cmdline_help();
        fuse_lowlevel_help();
        free(opts.mountpoint);
        fuse_opt_free_args(&args);
        return 0;
    }

    if (opts.show_version) {
        std::cout << "FUSE library version " << fuse_pkgversion() << std::endl;
        fuse_lowlevel_version();
        free(opts.mountpoint);
        fuse_opt_free_args(&args);
        return 0;
    }

    if (!opts.mountpoint) {
        std::cerr << "Error: no mountpoint specified." << std::endl;
        std::cerr << "usage: " << argv[0] << " [options] <mountpoint>" << std::endl;
        fuse_opt_free_args(&args);
        return 1;
    }

    // Update config with actual mountpoint
    config.mountpoint = opts.mountpoint;
    set_global_config(config);

    // ── Validate mountpoint ─────────────────────────────────
    if (!validate_mountpoint(opts.mountpoint)) {
        std::cerr << "\nFix the mountpoint issue and try again." << std::endl;
        free(opts.mountpoint);
        fuse_opt_free_args(&args);
        return 1;
    }

    // ── Initialize filesystem tree via mmap ─────────────────
    if (!mmap_init_treefile(persist_path.c_str(), file1, persist_fd, persist_mapsize)) {
        std::cerr << "Error: Could not init/load mmap persistence file." << std::endl;
        free(opts.mountpoint);
        fuse_opt_free_args(&args);
        return 1;
    }

    // Initialize the library catalog
    init_library_catalog(FASTDEVFS_LIB_CONFIG);

    // ── Start deduplication server ──────────────────────────
    start_dedup_server(dedup_path.c_str());

    // ── Start IPC server ────────────────────────────────────
    start_ipc_server(config.socket_path);
    write_pid_file(config.pid_path);

    // ── Create FUSE session ─────────────────────────────────
    struct fuse_lowlevel_ops ops = get_fuse_ll_ops();

    struct fuse_session *se = fuse_session_new(&args, &ops, sizeof(ops), file1);
    if (!se) {
        std::cerr << "Error: fuse_session_new() failed." << std::endl;
        stop_ipc_server();
        remove_pid_file(config.pid_path);
        mmap_close_treefile(file1, persist_fd, persist_mapsize);
        free(opts.mountpoint);
        fuse_opt_free_args(&args);
        return 1;
    }

    if (fuse_set_signal_handlers(se) != 0) {
        std::cerr << "Error: fuse_set_signal_handlers() failed." << std::endl;
        fuse_session_destroy(se);
        stop_ipc_server();
        remove_pid_file(config.pid_path);
        mmap_close_treefile(file1, persist_fd, persist_mapsize);
        free(opts.mountpoint);
        fuse_opt_free_args(&args);
        return 1;
    }

    if (fuse_session_mount(se, opts.mountpoint) != 0) {
        int err = errno;
        std::cerr << "\nError: fuse_session_mount() failed at '" << opts.mountpoint << "'." << std::endl;
        
        if (err != 0) {
            std::cerr << "System error: " << strerror(err) << std::endl;
        }
        
        // Provide troubleshooting hints
        std::cerr << "\nTroubleshooting:" << std::endl;
        std::cerr << "  1. Verify FUSE is installed: apt install fuse3 (on Ubuntu/Debian)" << std::endl;
        std::cerr << "  2. Verify user can use FUSE: usermod -a -G fuse $(whoami)" << std::endl;
        std::cerr << "  3. Check mountpoint permissions: ls -ld " << opts.mountpoint << std::endl;
        std::cerr << "  4. Ensure mountpoint is empty: find " << opts.mountpoint << " -type f" << std::endl;
        std::cerr << "  5. Check if already mounted: mount | grep " << opts.mountpoint << std::endl;
        
        fuse_remove_signal_handlers(se);
        fuse_session_destroy(se);
        stop_ipc_server();
        remove_pid_file(config.pid_path);
        stop_dedup_server();
        mmap_close_treefile(file1, persist_fd, persist_mapsize);
        free(opts.mountpoint);
        fuse_opt_free_args(&args);
        return 1;
    }

    std::cout << "FastDevFs mounted at " << opts.mountpoint << std::endl;

    // ── Enter event loop ────────────────────────────────────
    int ret;
    if (opts.foreground) {
        ret = fuse_session_loop(se);
    } else {
        ret = fuse_daemonize(opts.foreground);
        if (ret == 0) {
            ret = fuse_session_loop(se);
        }
    }

    // ── Cleanup ─────────────────────────────────────────────
    std::cout << "FastDevFs unmounting..." << std::endl;
    fuse_session_unmount(se);
    fuse_remove_signal_handlers(se);
    fuse_session_destroy(se);
    free(opts.mountpoint);
    fuse_opt_free_args(&args);

    // Stop IPC server
    stop_ipc_server();
    remove_pid_file(config.pid_path);

    // Stop dedup server
    stop_dedup_server();

    // Sync and close mmap
    mmap_close_treefile(file1, persist_fd, persist_mapsize);

    return ret ? 1 : 0;
}
