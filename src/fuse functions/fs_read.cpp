#include "../../include/fuse functions/fs_read.h"

int fs_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi) {
    (void) buf;
    (void) size;
    (void) offset;
    (void) fi;

    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    int index = hashindex(path, file1);
    if (index == -1) {
        return -ENOENT;
    }

    if (S_ISDIR(file1.arr[index].metadata.mode)) {
        return -EISDIR;
    }

    // No data storage implemented yet.
    // Return 0 bytes read (EOF).
    return 0;
}
