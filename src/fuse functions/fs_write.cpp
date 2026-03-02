#include "../../include/fuse functions/fs_write.h"
#include <algorithm>

int fs_write(const char *path, const char *buf, size_t size, off_t offset,
             struct fuse_file_info *fi) {
    (void) fi;
    
    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    int index = hashindex(path, file1);
    if (index == -1) {
        return -ENOENT;
    }
    
    // Check if directory
    if (S_ISDIR(file1.arr[index].metadata.mode)) {
        return -EISDIR;
    }

    metadate& meta = file1.arr[index].metadata;

    // Clamp writes to the in-line buffer
    if (offset >= MAX_FILE_DATA) {
        return -EFBIG;
    }

    size_t write_size = size;
    if ((size_t)offset + write_size > MAX_FILE_DATA) {
        write_size = MAX_FILE_DATA - offset;
    }

    if (write_size == 0) {
        return 0;
    }

    // Actually store the data
    memcpy(file1.arr[index].data + offset, buf, write_size);

    // Extend logical size when writing past current EOF
    if ((off_t)(offset + write_size) > meta.size) {
        meta.size = offset + write_size;
    }

    meta.mtime = time(NULL);
    meta.ctime = time(NULL);

    persist(file1);
    return write_size;
}
