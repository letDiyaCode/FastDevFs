#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <iostream>
#include <cstring>
#include "../include/daemon/directory tree/adt.h"
#include "../include/fuse functions/fuse_ops_init.h"

// Global treefile instance
treefile file1;

int main(int argc, char *argv[]) {
    // Initialize or load the file system tree from disk
    if (!init_or_load_treefile(FASTDEVFS_PERSIST_PATH, file1)) {
        std::cerr << "Warning: Could not load or create persistence file, initializing fresh." << std::endl;
        initialize(file1);
    }

    // Initialize FUSE operations
    struct fuse_operations ops;
    init_fuse_operations(ops);

    // Run FUSE main loop
    // We pass the global treefile instance as user_data
    int ret = fuse_main(argc, argv, &ops, &file1);

    // Safety-net save after FUSE exits (destroy callback should have saved already,
    // but this covers abnormal exits where destroy might not fire)
    if (!save_treefile(FASTDEVFS_PERSIST_PATH, file1)) {
        std::cerr << "Warning: Final save_treefile failed." << std::endl;
    }

    return ret;
}
