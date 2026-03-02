#include "../../include/fuse functions/fs_read.h"

int fs_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi) {
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

    metadate& meta = file1.arr[index].metadata;

    // Past end of file → EOF
    if (offset >= meta.size) {
        return 0;
    }

    // Clamp read to available data
    size_t available = meta.size - offset;
    size_t read_size = (size < available) ? size : available;

    // Also clamp to the physical buffer limit
    if ((size_t)offset + read_size > MAX_FILE_DATA) {
        if ((size_t)offset >= MAX_FILE_DATA) {
            return 0;
        }
        read_size = MAX_FILE_DATA - offset;
    }

    memcpy(buf, file1.arr[index].data + offset, read_size);

    meta.atime = time(NULL);

    return read_size;
}
