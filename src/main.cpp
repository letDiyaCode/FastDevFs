#define FUSE_USE_VERSION 31

#include <fuse3/fuse_lowlevel.h>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include "../include/daemon/directory tree/adt.h"
#include "../include/daemon/fuse_lowlevel_ops.h"

#define FASTDEVFS_PERSIST_PATH "/tmp/fastdevfs.mmap"

// Global treefile pointer (mmap'd) and mmap state
static treefile* file1 = nullptr;
static int persist_fd = -1;
static size_t persist_mapsize = 0;

int main(int argc, char *argv[]) {
    // Parse FUSE command line (extracts mountpoint, foreground, etc.)
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

    // Initialize or load the file system tree via mmap
    if (!mmap_init_treefile(FASTDEVFS_PERSIST_PATH, file1, persist_fd, persist_mapsize)) {
        std::cerr << "Error: Could not init/load mmap persistence file." << std::endl;
        free(opts.mountpoint);
        fuse_opt_free_args(&args);
        return 1;
    }

    // Get the low-level operations struct
    struct fuse_lowlevel_ops ops = get_fuse_ll_ops();

    // Create the FUSE session, with treefile* as userdata
    struct fuse_session *se = fuse_session_new(&args, &ops, sizeof(ops), file1);
    if (!se) {
        std::cerr << "Error: fuse_session_new() failed." << std::endl;
        mmap_close_treefile(file1, persist_fd, persist_mapsize);
        free(opts.mountpoint);
        fuse_opt_free_args(&args);
        return 1;
    }

    // Install signal handlers (SIGINT, SIGTERM → clean exit)
    if (fuse_set_signal_handlers(se) != 0) {
        std::cerr << "Error: fuse_set_signal_handlers() failed." << std::endl;
        fuse_session_destroy(se);
        mmap_close_treefile(file1, persist_fd, persist_mapsize);
        free(opts.mountpoint);
        fuse_opt_free_args(&args);
        return 1;
    }

    // Mount at the specified mountpoint
    if (fuse_session_mount(se, opts.mountpoint) != 0) {
        std::cerr << "Error: fuse_session_mount() failed." << std::endl;
        fuse_remove_signal_handlers(se);
        fuse_session_destroy(se);
        mmap_close_treefile(file1, persist_fd, persist_mapsize);
        free(opts.mountpoint);
        fuse_opt_free_args(&args);
        return 1;
    }

    std::cout << "FastDevFs mounted at " << opts.mountpoint << std::endl;

    // Enter event loop (single-threaded or daemonized based on -f flag)
    int ret;
    if (opts.foreground) {
        ret = fuse_session_loop(se);
    } else {
        ret = fuse_daemonize(opts.foreground);
        if (ret == 0) {
            ret = fuse_session_loop(se);
        }
    }

    // Cleanup
    std::cout << "FastDevFs unmounting..." << std::endl;
    fuse_session_unmount(se);
    fuse_remove_signal_handlers(se);
    fuse_session_destroy(se);
    free(opts.mountpoint);
    fuse_opt_free_args(&args);

    // Sync and close mmap
    mmap_close_treefile(file1, persist_fd, persist_mapsize);

    return ret ? 1 : 0;
}
