#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <iostream>
#include <cstring>
#include "../include/daemon/directory tree/adt.h"
#include "../include/fuse functions/fuse_ops_init.h"

// Global treefile instance
treefile file1;

int main(int argc, char *argv[]) {
    // Initialize the file system tree
    initialize(file1);

    // Initialize FUSE operations
    struct fuse_operations ops;
    init_fuse_operations(ops);

    // Run FUSE main loop
    // We pass the global treefile instance as user_data
    return fuse_main(argc, argv, &ops, &file1);
}
