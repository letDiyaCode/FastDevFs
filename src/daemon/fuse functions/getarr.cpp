#include <errno.h>
#include <fuse.h>       // or <fuse3/fuse.h> depending on your FUSE version
#include <string>
using std::string;

// assume there's a global treefile instance used by FUSE callbacks:
extern treefile g_treefile;

// -------------- getarr helper ----------------
// Template-based helper that returns the metadata struct by output parameter.
// Usage:
//   decltype(g_treefile.arr[0].metadata) meta;
//   if (!getarr("filename", meta, g_treefile)) { /* not found */ }
template<typename Meta>
bool getarr(const string &name_in, Meta &out_meta, treefile &file1) {
    lock_guard<recursive_mutex> lock(file1.mtx);

    // normalize name: files in your tree appear stored without leading '/'
    string name = name_in;
    if (!name.empty() && name[0] == '/') name = name.substr(1);

    // root case: empty name maps to index 0
    if (name.empty()) {
        if (0 <= 0 && 0 < file1.head.size && !file1.arr[0].isdeleted) {
            out_meta = file1.arr[0].metadata;
            return true;
        } else {
            return false;
        }
    }

    // validate tree
    if (file1.head.size <= 0 || file1.head.size > 100000) return false;

    if (!file1.head.hash.has(name)) return false;

    int idx = hashindex(name, file1);
    if (idx < 0 || idx >= file1.head.size) return false;
    if (file1.arr[idx].isdeleted) return false;

    out_meta = file1.arr[idx].metadata;
    return true;
}

// -------------- FUSE getattr callback ---------------
// This implements the standard FUSE getattr and fills struct stat from your stored metadata.
// Signature below matches libfuse3. If you're using an older libfuse, remove the fuse_file_info param.
static int fs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi; // unused

    // zero the stat buffer first
    memset(stbuf, 0, sizeof(struct stat));

    // get metadata from tree
    decltype(g_treefile.arr[0].metadata) meta;
    if (!getarr(string(path), meta, g_treefile)) {
        return -ENOENT;
    }

    // copy fields (cast where appropriate)
    stbuf->st_mode  = static_cast<mode_t>(meta.mode);
    stbuf->st_uid   = static_cast<uid_t>(meta.uid);
    stbuf->st_gid   = static_cast<gid_t>(meta.gid);
    stbuf->st_size  = static_cast<off_t>(meta.size);
    stbuf->st_nlink = static_cast<nlink_t>(meta.nlink);
    stbuf->st_ino   = static_cast<ino_t>(meta.inode);

    // times (stat has st_atim/st_mtim on some platforms; we'll set the simple fields)
#if defined(__APPLE__) || defined(__MACH__)
    stbuf->st_atimespec.tv_sec = static_cast<time_t>(meta.atime);
    stbuf->st_mtimespec.tv_sec = static_cast<time_t>(meta.mtime);
    stbuf->st_ctimespec.tv_sec = static_cast<time_t>(meta.ctime);
#else
    stbuf->st_atime = static_cast<time_t>(meta.atime);
    stbuf->st_mtime = static_cast<time_t>(meta.mtime);
    stbuf->st_ctime = static_cast<time_t>(meta.ctime);
#endif

    return 0;
}
