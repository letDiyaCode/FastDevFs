#include "../../include/fuse functions/fuse_ops_init.h"
#include "../../include/fuse functions/fs_getattr.h"
#include "../../include/fuse functions/fs_access.h"
#include "../../include/fuse functions/fs_mkdir.h"
#include "../../include/fuse functions/fs_opendir.h"
#include "../../include/fuse functions/fs_readdir.h"
#include "../../include/fuse functions/fs_rmdir.h"
#include "../../include/fuse functions/fs_statfs.h"
#include "../../include/fuse functions/fs_utimens.h"
#include "../../include/fuse functions/fs_create.h"
#include "../../include/fuse functions/fs_open.h"
#include "../../include/fuse functions/fs_read.h"
#include "../../include/fuse functions/fs_write.h"
#include "../../include/fuse functions/fs_truncate.h"
#include "../../include/fuse functions/fs_unlink.h"
#include "../../include/fuse functions/fs_chmod.h"
#include "../../include/fuse functions/fs_chown.h"

// Called by FUSE on clean unmount (fusermount -u / umount)
static void fs_destroy(void *private_data) {
    if (!private_data) return;
    treefile& file1 = *static_cast<treefile*>(private_data);
    if (!save_treefile(FASTDEVFS_PERSIST_PATH, file1)) {
        std::cerr << "[fastdevfs] ERROR: Failed to save treefile on unmount!" << std::endl;
    } else {
        std::cerr << "[fastdevfs] Treefile saved successfully on unmount." << std::endl;
    }
}

void init_fuse_operations(struct fuse_operations& ops) {
    memset(&ops, 0, sizeof(struct fuse_operations));
    
    ops.getattr = fs_getattr;
    ops.access = fs_access;
    ops.mkdir = fs_mkdir;
    ops.opendir = fs_opendir;
    ops.readdir = fs_readdir;
    ops.rmdir = fs_rmdir;
    ops.statfs = fs_statfs;
    ops.utimens = fs_utimens;
    ops.create = fs_create;
    ops.open = fs_open;
    ops.read = fs_read;
    ops.write = fs_write;
    ops.truncate = fs_truncate;
    ops.unlink = fs_unlink;
    ops.chmod = fs_chmod;
    ops.chown = fs_chown;
    ops.destroy = fs_destroy;
}
