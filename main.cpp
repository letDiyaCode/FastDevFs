#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <cstring>
#include <string>
#include <unordered_map>
#include <iostream>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <vector>

using namespace std;

// Simple in-memory file store: full path -> content
static unordered_map<string, string> files;

static void populate_example_files() {
    files["/hello.txt"] = "Hello, FUSE!\n";
    files["/notes.md"]   = "# notes\n- item1\n";
}

/*
 * getattr
 */
int get_attr(const char* path, struct stat* stbuf, struct fuse_file_info* fi){
    cout << "getattr called for path: " << path << endl;
    (void) fi; // unused

    memset(stbuf, 0, sizeof(struct stat));
    string p(path);

    // Root directory
    if (p == "/") {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
        time_t now = time(nullptr);
        stbuf->st_mtime = now;
        stbuf->st_atime = now;
        stbuf->st_ctime = now;
        return 0;
    }

    // Regular file?
    auto it = files.find(p);
    if (it != files.end()) {
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_nlink = 1;
        stbuf->st_size = static_cast<off_t>(it->second.size());
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
        time_t now = time(nullptr);
        stbuf->st_mtime = now;
        stbuf->st_atime = now;
        stbuf->st_ctime = now;
        // Optional block fields:
        // stbuf->st_blksize = 4096;
        // stbuf->st_blocks = (stbuf->st_size + 511) / 512;
        return 0;
    }

    return -ENOENT;
}

/*
 * readdir
 * Note: filler has signature
 *   int filler(void *buf, const char *name, const struct stat *stbuf, off_t off, enum fuse_fill_dir_flags flags);
 */
int readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags){
    cout << "readdir called for path: " << path << endl;
    (void) offset;
    (void) fi;
    (void) flags; // not used

    string p(path);
    if (p != "/") {
        return -ENOENT;
    }

    // Always add . and ..
    if (filler(buf, ".", NULL, 0, static_cast<fuse_fill_dir_flags>(0)) != 0)
        return 0;
    if (filler(buf, "..", NULL, 0, static_cast<fuse_fill_dir_flags>(0)) != 0)
        return 0;

    // Add files that live in root (strip leading '/')
    for (const auto& kv : files) {
        const string& full = kv.first;
        if (full.size() == 0) continue;
        // only top-level entries (no subdirs handled by this simple FS)
        if (full[0] == '/') {
            string name = full.substr(1);
            // ignore entries containing '/' (not top-level)
            if (name.find('/') != string::npos) continue;
            if (filler(buf, name.c_str(), NULL, 0, static_cast<fuse_fill_dir_flags>(0)) != 0)
                return 0; // buffer full
        }
    }

    return 0;
}

static struct fuse_operations fastdevfs_oper = {
    .getattr = get_attr,
    .readdir = readdir,
};

int main(int argc, char* argv[]){
    cout << "Starting FastDevFs Daemon..." << endl;
    populate_example_files();

    return fuse_main(argc, argv, &fastdevfs_oper, NULL);
}

