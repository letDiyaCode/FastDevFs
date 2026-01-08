#include "fuse_functions/readdir.h"
#include <cstring>
#include <cerrno>

int fastdevfs_readdir(
    const char* path,
    void* buf,
    fuse_fill_dir_t filler,
    off_t offset,
    struct fuse_file_info* fi,
    enum fuse_readdir_flags flags
) {
    (void) offset;
    (void) fi;
    (void) flags;

    // Only support the root directory for now
    if (strcmp(path, "/") != 0) {
        return -ENOENT;
    }

    // Mandatory entries
    filler(buf, ".",  nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "..", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));

    return 0;
}
