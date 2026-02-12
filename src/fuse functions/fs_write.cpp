#include "../../include/fuse functions/fs_write.h"
#include <algorithm>

int fs_write(const char *path, const char *buf, size_t size, off_t offset,
             struct fuse_file_info *fi) {
    (void) buf;
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

    // Mock write: update size if offset + size > current size
    metadate& meta = file1.arr[index].metadata;
    if (offset + size > (size_t)meta.size) {
        meta.size = offset + size;
    }
    
    meta.mtime = time(NULL);
    meta.ctime = time(NULL);

    return size; // Pretend we wrote everything
}
