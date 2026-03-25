#ifndef FUSE_LOWLEVEL_OPS_H
#define FUSE_LOWLEVEL_OPS_H

#define FUSE_USE_VERSION 31

#include <fuse3/fuse_lowlevel.h>
#include "directory tree/adt.h"

// Directory on the host filesystem where file data is stored.
// Each file is named by its tree array index (e.g., "5" for arr[5]).
#define FASTDEVFS_DATA_DIR "/tmp/fastdevfs_data"

// Convert between fuse_ino_t and tree array index.
// FUSE reserves ino 0 as invalid; FUSE_ROOT_ID == 1.
// So: ino = index + 1, index = ino - 1.
static inline int ino_to_index(fuse_ino_t ino) {
    return (int)(ino - 1);
}

static inline fuse_ino_t index_to_ino(int index) {
    return (fuse_ino_t)(index + 1);
}

// Returns a fully populated fuse_lowlevel_ops struct.
struct fuse_lowlevel_ops get_fuse_ll_ops();

#endif /* FUSE_LOWLEVEL_OPS_H */
