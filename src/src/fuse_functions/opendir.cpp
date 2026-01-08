#include "fuse_functions/opendir.h"
#include <cstring>
#include <cerrno>

int fastdevfs_opendir(
    const char* path,
    struct fuse_file_info* fi
) {
    (void) fi;

    // Only support the root directory for now
    if (strcmp(path, "/") != 0) {
        return -ENOENT;
    }

    // Allow opening the root directory
    return 0;
}
