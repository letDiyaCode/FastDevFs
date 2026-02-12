#include "../../include/fuse functions/fs_readdir.h"

int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi,
               enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    treefile& file1 = get_treefile();
    std::lock_guard<std::recursive_mutex> lock(file1.mtx);

    int index = -1;
    std::string path_str(path);

    if (path_str == "/") {
        index = 0;
    } else {
        index = hashindex(path_str, file1);
    }

    if (index == -1) {
        return -ENOENT;
    }

    if (!S_ISDIR(file1.arr[index].metadata.mode)) {
        return -ENOTDIR;
    }

    filler(buf, ".", NULL, 0, (fuse_fill_dir_flags)0);
    filler(buf, "..", NULL, 0, (fuse_fill_dir_flags)0);

    int child_idx = file1.arr[index].firstchild;
    while (child_idx != -1) {
        if (child_idx >= file1.head.size || child_idx < 0) break; // Safety check
        
        if (!file1.arr[child_idx].isdeleted) {
            std::string full_child_path = file1.arr[child_idx].metadata.name;
            std::string filename = get_filename(full_child_path);
            
            struct stat st;
            memset(&st, 0, sizeof(st));
            st.st_ino = child_idx; // Use index as inode
            st.st_mode = file1.arr[child_idx].metadata.mode;
            
            if (filler(buf, filename.c_str(), &st, 0, (fuse_fill_dir_flags)0)) {
                break;
            }
        }
        child_idx = file1.arr[child_idx].nextsibling;
    }

    return 0;
}
