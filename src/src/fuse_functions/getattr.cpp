#include "fuse_functions/getattr.h"
#include <cstring>
#include <cerrno>
#include <unistd.h>

using namespace std;

int fastdevfs_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
    // cout<<"getattr called for path: "<<path<<endl;

    (void) fi;
    memset(stbuf, 0, sizeof(struct stat));

    if(strcmp("/", path) == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
        return 0;
    }
    return -ENOENT;
}