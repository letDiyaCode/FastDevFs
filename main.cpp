// #define FUSE_USE_VERSION 31

// #include<fuse3/fuse.h>
// #include<iostream>
// #include "src/include/fuse_functions/getattr.h"
// #include "src/include/fuse_functions/readdir.h"
// #include "src/include/fuse_functions/opendir.h"

// using namespace std;

// // int get_attr(const char* path, struct stat* stbuf, struct fuse_file_info* fi){
// //     cout<<"getattr called for path: "<<path<<endl;
// //     // Implementation of getattr would go here
// //     return 0;
// // }
// // int readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags){
// //     cout<<"readdir called for path: "<<path<<endl;
// //     // Implementation of readdir would go here
// //     return 0;
// // }

// static struct fuse_operations fastdevfs_oper = {
//     // .getattr = get_attr,
//     // .readdir = readdir,
//     .getattr = fastdevfs_getattr,
//     .opendir = fastdevfs_opendir,
//     .readdir = fastdevfs_readdir,
// };
// int main(int argc, char* argv[]){
//     cout<<"Starting FastDevFs Daemon..."<<endl;
//     // Initialization code for FastDevFs would go here

//     return fuse_main(argc, argv, &fastdevfs_oper, NULL);
// }



#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <iostream>

#include "daemon/dir_manager.h"
#include "fuse_functions/getattr.h"
#include "fuse_functions/readdir.h"
#include "fuse_functions/opendir.h"
#include "fuse_functions/mkdir.h"
#include "fuse_functions/rmdir.h"
#include "fuse_functions/access.h"
#include "fuse_functions/statfs.h"

using namespace std;

/*
 * Global DirManager instance.
 * Used by all FUSE callbacks.
 */
DirManager* g_dir_manager = nullptr;

static struct fuse_operations fdfs_ops;

// static struct fuse_operations fdfs_ops = {
//     .getattr   = fdfs_getattr,
//     .readdir   = fdfs_readdir,
//     .opendir   = fdfs_opendir,
//     // .mkdir     = fdfs_mkdir,
//     // .rmdir     = fdfs_rmdir,
//     // .access    = fdfs_access,
//     // .statfs    = fdfs_statfs,

//     // // safety stubs
//     // .open      = fdfs_open,
//     // .read      = fdfs_read,
//     // .write     = fdfs_write,
//     // .create    = fdfs_create,
//     // .unlink    = fdfs_unlink,
//     // .truncate  = fdfs_truncate,

//     // // metadata no-ops
//     // .chmod     = fdfs_chmod,
//     // .chown     = fdfs_chown,
//     // .utimens   = fdfs_utimens,
// };


int main(int argc, char* argv[]) {
    cout << "Starting FastDevFS Daemon..." << endl;

    // Initialize directory ADT
    dir_manager_init(g_dir_manager);



    // basic fuse_functions
    fdfs_ops.getattr = fdfs_getattr;
    fdfs_ops.opendir = fdfs_opendir;
    fdfs_ops.readdir = fdfs_readdir;
    fdfs_ops.mkdir   = fdfs_mkdir;
    fdfs_ops.rmdir   = fdfs_rmdir;
    fdfs_ops.access  = fdfs_access;
    fdfs_ops.statfs  = fdfs_statfs;

    // file ops
    // fdfs_ops.open    = fdfs_open;
    // fdfs_ops.read      = fdfs_read,
    // fdfs_ops.write     = fdfs_write,
    // fdfs_ops.create    = fdfs_create,
    // fdfs_ops.unlink    = fdfs_unlink,
    // fdfs_ops.truncate  = fdfs_truncate,

    // Start FUSE
    return fuse_main(argc, argv, &fdfs_ops, nullptr);
}