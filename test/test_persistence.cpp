#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <vector>
#include <atomic>
#include <fstream>
#include "../include/daemon/directory tree/adt.h"

class PersistenceTest : public ::testing::Test {
protected:
    const char* test_file = "/tmp/test_treefile.mmap";

    void SetUp() override {
        unlink(test_file);  // Remove file if exists
    }

    void TearDown() override {
        unlink(test_file);  // Cleanup
    }
};

// Test 1: BasicSaveLoad
// Save a tree with root -> dir1 -> file1, load into a new treefile, verify structure
TEST_F(PersistenceTest, BasicSaveLoad) {
    // Create and populate source tree
    treefile* src = new treefile();
    initialize(*src);

    insertfolder("dir1", "/", *src);
    insertfile("file1", "dir1", *src);

    // Verify source tree structure
    ASSERT_TRUE(src->head.hash.has("dir1"));
    ASSERT_TRUE(src->head.hash.has("file1"));

    int dir1Idx = hashindex("dir1", *src);
    int file1Idx = hashindex("file1", *src);
    ASSERT_NE(dir1Idx, -1);
    ASSERT_NE(file1Idx, -1);
    EXPECT_EQ(src->arr[file1Idx].parent, dir1Idx);

    // Save to disk
    ASSERT_TRUE(save_treefile(test_file, *src));
    delete src;

    // Load into new treefile
    treefile* dst = new treefile();
    initialize(*dst);  // Initialize first (sets up hash.m)
    ASSERT_TRUE(load_treefile(test_file, *dst));

    // Verify all nodes exist in loaded tree
    EXPECT_TRUE(dst->head.hash.has("/"));
    EXPECT_TRUE(dst->head.hash.has("dir1"));
    EXPECT_TRUE(dst->head.hash.has("file1"));

    // Verify parent-child relationships preserved
    int loadedDir1 = hashindex("dir1", *dst);
    int loadedFile1 = hashindex("file1", *dst);
    ASSERT_NE(loadedDir1, -1);
    ASSERT_NE(loadedFile1, -1);
    EXPECT_EQ(dst->arr[loadedFile1].parent, loadedDir1);

    // Verify hash map correctly restored
    EXPECT_EQ(hashindex("dir1", *dst), loadedDir1);
    EXPECT_EQ(hashindex("file1", *dst), loadedFile1);

    delete dst;
}

// Test 2: PersistenceAcrossRestarts
// Simulate daemon restart: create tree, save, destroy, load in new scope
TEST_F(PersistenceTest, PersistenceAcrossRestarts) {
    int savedNodeCount = 0;

    // First "run" — create and save
    {
        treefile* file = new treefile();
        initialize(*file);

        insertfolder("projects", "/", *file);
        insertfolder("src", "projects", *file);
        insertfile("main.cpp", "src", *file);
        insertfile("readme.md", "projects", *file);

        savedNodeCount = file->head.nodeallocated;
        ASSERT_TRUE(save_treefile(test_file, *file));
        delete file;
    }
    // treefile destroyed — simulates process exit

    // Second "run" — load and verify
    {
        treefile* file = new treefile();
        initialize(*file);
        ASSERT_TRUE(load_treefile(test_file, *file));

        // Verify all data restored
        EXPECT_TRUE(file->head.hash.has("projects"));
        EXPECT_TRUE(file->head.hash.has("src"));
        EXPECT_TRUE(file->head.hash.has("main.cpp"));
        EXPECT_TRUE(file->head.hash.has("readme.md"));

        // Verify nodeallocated counter correctly restored
        EXPECT_EQ(file->head.nodeallocated, savedNodeCount);

        // Verify we can continue operations on loaded tree
        insertfile("new_after_load.txt", "projects", *file);
        EXPECT_TRUE(file->head.hash.has("new_after_load.txt"));
        EXPECT_EQ(file->head.nodeallocated, savedNodeCount + 1);

        delete file;
    }
}

// Test 3: SaveAfterModifications
// Save, then modify, then reload — modifications should be discarded
TEST_F(PersistenceTest, SaveAfterModifications) {
    treefile* file = new treefile();
    initialize(*file);

    insertfile("keep", "/", *file);
    insertfile("temp1", "/", *file);
    insertfile("temp2", "/", *file);

    // Save the tree (with keep, temp1, temp2)
    ASSERT_TRUE(save_treefile(test_file, *file));

    // Now delete temp1 and temp2 (in-memory only)
    delete1("temp1", *file);
    delete1("temp2", *file);
    EXPECT_FALSE(file->head.hash.has("temp1"));
    EXPECT_FALSE(file->head.hash.has("temp2"));

    // Reload from disk — should restore pre-deletion state
    ASSERT_TRUE(load_treefile(test_file, *file));

    // temp1 and temp2 should be back
    EXPECT_TRUE(file->head.hash.has("keep"));
    EXPECT_TRUE(file->head.hash.has("temp1"));
    EXPECT_TRUE(file->head.hash.has("temp2"));

    delete file;
}

// Test 4: LoadNonExistentFile
TEST_F(PersistenceTest, LoadNonExistentFile) {
    treefile* file = new treefile();
    initialize(*file);

    bool result = load_treefile("/tmp/nonexistent_treefile_xyzzy.mmap", *file);
    EXPECT_FALSE(result);

    delete file;
}

// Test 5: LoadCorruptedFile
// Create a file with wrong size, verify load rejects it
TEST_F(PersistenceTest, LoadCorruptedFile) {
    // Create a file with wrong size (just some text, not a valid treefile)
    {
        std::ofstream f(test_file);
        f << "this is not a valid treefile";
        f.close();
    }

    treefile* file = new treefile();
    initialize(*file);

    bool result = load_treefile(test_file, *file);
    EXPECT_FALSE(result);

    delete file;
}

// Test 6: InitOrLoadCreatesNew
// When file doesn't exist, init_or_load should initialize and create the file
TEST_F(PersistenceTest, InitOrLoadCreatesNew) {
    treefile* file = new treefile();

    bool result = init_or_load_treefile(test_file, *file);
    EXPECT_TRUE(result);

    // Verify tree is in initialized state
    EXPECT_EQ(file->head.firstfree, 1);
    EXPECT_EQ(file->head.nodeallocated, 1);
    EXPECT_TRUE(file->head.hash.has("/"));

    // Verify file now exists on disk
    struct stat st;
    EXPECT_EQ(stat(test_file, &st), 0);

    delete file;
}

// Test 7: InitOrLoadLoadsExisting
// When file exists, init_or_load should load data, not re-initialize
TEST_F(PersistenceTest, InitOrLoadLoadsExisting) {
    // First: create and save a tree with some data
    {
        treefile* file = new treefile();
        initialize(*file);
        insertfolder("existing_dir", "/", *file);
        insertfile("existing_file", "existing_dir", *file);
        ASSERT_TRUE(save_treefile(test_file, *file));
        delete file;
    }

    // Now: init_or_load should load the existing data
    treefile* file = new treefile();
    bool result = init_or_load_treefile(test_file, *file);
    EXPECT_TRUE(result);

    // Data should be loaded, not fresh
    EXPECT_TRUE(file->head.hash.has("existing_dir"));
    EXPECT_TRUE(file->head.hash.has("existing_file"));

    // Verify the node count reflects loaded data (root + 2 nodes)
    EXPECT_EQ(file->head.nodeallocated, 3);

    delete file;
}

// Test 8: PersistenceWithDeepTree
// Create a 10-level deep tree, save, load, verify all levels
TEST_F(PersistenceTest, PersistenceWithDeepTree) {
    treefile* src = new treefile();
    initialize(*src);

    // Create: / -> dir0 -> dir1 -> ... -> dir9
    std::string parent = "/";
    for (int i = 0; i < 10; ++i) {
        std::string name = "dir" + std::to_string(i);
        insertfolder(name, parent, *src);
        parent = name;
    }

    ASSERT_TRUE(save_treefile(test_file, *src));
    delete src;

    // Load and verify all 10 levels
    treefile* dst = new treefile();
    initialize(*dst);
    ASSERT_TRUE(load_treefile(test_file, *dst));

    for (int i = 0; i < 10; ++i) {
        std::string name = "dir" + std::to_string(i);
        EXPECT_TRUE(dst->head.hash.has(name)) << "Missing: " << name;
        int idx = hashindex(name, *dst);
        EXPECT_NE(idx, -1) << "Bad index for: " << name;
        EXPECT_FALSE(dst->arr[idx].isdeleted) << "Deleted: " << name;
    }

    // Verify parent chain: dir9's ancestor chain goes up to root
    int idx = hashindex("dir9", *dst);
    int depth = 0;
    while (idx > 0 && depth < 20) {
        idx = dst->arr[idx].parent;
        depth++;
    }
    // Should have walked through dir8..dir0 + root = 10 hops
    EXPECT_EQ(depth, 10);

    delete dst;
}

// Test 9: PersistenceWithManyNodes
// Insert 1000 files, save, load, verify all present
TEST_F(PersistenceTest, PersistenceWithManyNodes) {
    treefile* src = new treefile();
    initialize(*src);

    for (int i = 0; i < 1000; ++i) {
        insertfile("file_" + std::to_string(i), "/", *src);
    }

    int srcAllocated = src->head.nodeallocated;
    EXPECT_EQ(srcAllocated, 1001);  // root + 1000 files

    ASSERT_TRUE(save_treefile(test_file, *src));
    delete src;

    // Load and verify
    treefile* dst = new treefile();
    initialize(*dst);
    ASSERT_TRUE(load_treefile(test_file, *dst));

    EXPECT_EQ(dst->head.nodeallocated, 1001);

    // Spot-check several files
    for (int i = 0; i < 1000; i += 100) {
        std::string name = "file_" + std::to_string(i);
        EXPECT_TRUE(dst->head.hash.has(name)) << "Missing: " << name;
        int idx = hashindex(name, *dst);
        EXPECT_NE(idx, -1);
        EXPECT_FALSE(dst->arr[idx].isdeleted);
    }

    delete dst;
}

// Test 10: ConcurrentSaveLoad
// Multiple threads saving concurrently — mutex should protect
TEST_F(PersistenceTest, ConcurrentSaveLoad) {
    treefile* file = new treefile();
    initialize(*file);

    // Insert 50 files
    for (int i = 0; i < 50; ++i) {
        insertfile("concurrent_" + std::to_string(i), "/", *file);
    }

    std::atomic<int> successCount(0);

    // Spawn 5 threads, each saving 10 times
    std::vector<std::thread> threads;
    for (int t = 0; t < 5; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 10; ++i) {
                if (save_treefile(test_file, *file)) {
                    successCount++;
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // All 50 saves should succeed (mutex protects)
    EXPECT_EQ(successCount.load(), 50);

    // Verify loaded data is valid
    treefile* loaded = new treefile();
    initialize(*loaded);
    ASSERT_TRUE(load_treefile(test_file, *loaded));

    // Tree should be consistent — all 50 files present
    for (int i = 0; i < 50; ++i) {
        std::string name = "concurrent_" + std::to_string(i);
        EXPECT_TRUE(loaded->head.hash.has(name)) << "Missing after concurrent save: " << name;
    }

    delete file;
    delete loaded;
}

// Test 11: HashMapPersistence
// Verify hash map entries and indices survive save/load
TEST_F(PersistenceTest, HashMapPersistence) {
    treefile* src = new treefile();
    initialize(*src);

    // Insert 100 files and record their indices
    std::vector<std::pair<std::string, int>> records;
    for (int i = 0; i < 100; ++i) {
        std::string name = "hash_node_" + std::to_string(i);
        insertfile(name, "/", *src);
        int idx = hashindex(name, *src);
        ASSERT_NE(idx, -1);
        records.push_back({name, idx});
    }

    ASSERT_TRUE(save_treefile(test_file, *src));
    delete src;

    // Load and verify all hash entries and indices match
    treefile* dst = new treefile();
    initialize(*dst);
    ASSERT_TRUE(load_treefile(test_file, *dst));

    for (const auto& [name, expectedIdx] : records) {
        EXPECT_TRUE(dst->head.hash.has(name)) << "Hash missing: " << name;
        int loadedIdx = hashindex(name, *dst);
        EXPECT_EQ(loadedIdx, expectedIdx) << "Index mismatch for: " << name;
    }

    delete dst;
}

// Test 12: FreeListPersistence
// Insert nodes, delete some to create free list holes, save/load, verify free list
TEST_F(PersistenceTest, FreeListPersistence) {
    treefile* src = new treefile();
    initialize(*src);

    // Insert 10 files
    for (int i = 0; i < 10; ++i) {
        insertfile("fl_" + std::to_string(i), "/", *src);
    }

    // Delete 5 of them (creates free list entries)
    for (int i = 0; i < 5; ++i) {
        delete1("fl_" + std::to_string(i), *src);
    }

    // Count free nodes in source by walking the free list
    int srcFreeCount = 0;
    {
        int f = src->head.firstfree;
        int limit = src->head.size;
        while (f != -1 && f < src->head.size && limit > 0) {
            srcFreeCount++;
            f = src->arr[f].nextfree;
            limit--;
        }
    }
    EXPECT_GT(srcFreeCount, 0);

    int srcAllocated = src->head.nodeallocated;
    ASSERT_TRUE(save_treefile(test_file, *src));
    delete src;

    // Load and verify free list
    treefile* dst = new treefile();
    initialize(*dst);
    ASSERT_TRUE(load_treefile(test_file, *dst));

    EXPECT_EQ(dst->head.nodeallocated, srcAllocated);

    // Count free nodes in loaded tree
    int dstFreeCount = 0;
    {
        int f = dst->head.firstfree;
        int limit = dst->head.size;
        while (f != -1 && f < dst->head.size && limit > 0) {
            dstFreeCount++;
            f = dst->arr[f].nextfree;
            limit--;
        }
    }
    EXPECT_EQ(dstFreeCount, srcFreeCount);

    // Verify we can allocate from the free list after load
    insertfile("after_load_alloc", "/", *dst);
    EXPECT_TRUE(dst->head.hash.has("after_load_alloc"));
    EXPECT_EQ(dst->head.nodeallocated, srcAllocated + 1);

    delete dst;
}

// Test 13: MetadataPersistence
// Verify metadata fields (inode, mode, uid, gid, times, etc.) survive save/load
TEST_F(PersistenceTest, MetadataPersistence) {
    treefile* src = new treefile();
    initialize(*src);

    insertfile("meta_file1", "/", *src);
    insertfile("meta_file2", "/", *src);

    // Set custom metadata
    int idx1 = hashindex("meta_file1", *src);
    int idx2 = hashindex("meta_file2", *src);
    ASSERT_NE(idx1, -1);
    ASSERT_NE(idx2, -1);

    src->arr[idx1].metadata.inode = 12345;
    src->arr[idx1].metadata.size = 4096;
    src->arr[idx1].metadata.mode = S_IFREG | 0755;
    src->arr[idx1].metadata.atime = 1000000;
    src->arr[idx1].metadata.mtime = 2000000;

    src->arr[idx2].metadata.inode = 67890;
    src->arr[idx2].metadata.size = 8192;
    src->arr[idx2].metadata.mode = S_IFREG | 0600;
    src->arr[idx2].metadata.atime = 3000000;
    src->arr[idx2].metadata.mtime = 4000000;

    ASSERT_TRUE(save_treefile(test_file, *src));
    delete src;

    // Load and verify metadata
    treefile* dst = new treefile();
    initialize(*dst);
    ASSERT_TRUE(load_treefile(test_file, *dst));

    int loadedIdx1 = hashindex("meta_file1", *dst);
    int loadedIdx2 = hashindex("meta_file2", *dst);
    ASSERT_NE(loadedIdx1, -1);
    ASSERT_NE(loadedIdx2, -1);

    EXPECT_EQ(dst->arr[loadedIdx1].metadata.inode, 12345);
    EXPECT_EQ(dst->arr[loadedIdx1].metadata.size, 4096);
    EXPECT_EQ(dst->arr[loadedIdx1].metadata.mode, (mode_t)(S_IFREG | 0755));
    EXPECT_EQ(dst->arr[loadedIdx1].metadata.atime, 1000000);
    EXPECT_EQ(dst->arr[loadedIdx1].metadata.mtime, 2000000);

    EXPECT_EQ(dst->arr[loadedIdx2].metadata.inode, 67890);
    EXPECT_EQ(dst->arr[loadedIdx2].metadata.size, 8192);
    EXPECT_EQ(dst->arr[loadedIdx2].metadata.mode, (mode_t)(S_IFREG | 0600));
    EXPECT_EQ(dst->arr[loadedIdx2].metadata.atime, 3000000);
    EXPECT_EQ(dst->arr[loadedIdx2].metadata.mtime, 4000000);

    delete dst;
}

// ============================================================
// mmap-based persistence verification tests
// These use the mmap() syscall directly to inspect on-disk data
// and simulate full unmount/remount cycles verifying the entire
// node array comes back byte-for-byte identical.
// ============================================================

// Test 14: MmapDirectDataInspection
// Save a treefile, then directly mmap the file and inspect the raw serialized data
TEST_F(PersistenceTest, MmapDirectDataInspection) {
    treefile* src = new treefile();
    initialize(*src);

    insertfolder("mmap_dir", "/", *src);
    insertfile("mmap_file", "mmap_dir", *src);

    int dirIdx = hashindex("mmap_dir", *src);
    int fileIdx = hashindex("mmap_file", *src);
    ASSERT_NE(dirIdx, -1);
    ASSERT_NE(fileIdx, -1);

    // Set distinctive metadata
    src->arr[fileIdx].metadata.inode = 42;
    src->arr[fileIdx].metadata.size = 1024;

    ASSERT_TRUE(save_treefile(test_file, *src));

    // Directly mmap the file — see exactly what sits on disk
    int fd = open(test_file, O_RDONLY);
    ASSERT_NE(fd, -1);

    struct stat st;
    ASSERT_EQ(fstat(fd, &st), 0);
    ASSERT_EQ((size_t)st.st_size, sizeof(treefile_serializable));

    void* mapped = mmap(NULL, sizeof(treefile_serializable), PROT_READ, MAP_PRIVATE, fd, 0);
    ASSERT_NE(mapped, MAP_FAILED);

    const treefile_serializable* raw = static_cast<const treefile_serializable*>(mapped);

    // Verify header fields on disk match in-memory state
    EXPECT_EQ(raw->head.firstfree, src->head.firstfree);
    EXPECT_EQ(raw->head.start, src->head.start);
    EXPECT_EQ(raw->head.size, src->head.size);
    EXPECT_EQ(raw->head.nodeallocated, src->head.nodeallocated);

    // Verify root node on disk
    EXPECT_FALSE(raw->arr[0].isdeleted);
    EXPECT_STREQ(raw->arr[0].metadata.name, "/");
    EXPECT_TRUE(S_ISDIR(raw->arr[0].metadata.mode));

    // Verify our file node on disk
    EXPECT_FALSE(raw->arr[fileIdx].isdeleted);
    EXPECT_STREQ(raw->arr[fileIdx].metadata.name, "mmap_file");
    EXPECT_EQ(raw->arr[fileIdx].metadata.inode, 42);
    EXPECT_EQ(raw->arr[fileIdx].metadata.size, 1024);
    EXPECT_EQ(raw->arr[fileIdx].parent, dirIdx);

    // Verify our dir node on disk
    EXPECT_FALSE(raw->arr[dirIdx].isdeleted);
    EXPECT_STREQ(raw->arr[dirIdx].metadata.name, "mmap_dir");
    EXPECT_TRUE(S_ISDIR(raw->arr[dirIdx].metadata.mode));

    // Verify hash map on disk has entries
    EXPECT_GT(raw->head.hashdata.size, 0u);

    // Verify a free-list node is marked deleted on disk
    int freeIdx = src->head.firstfree;
    if (freeIdx > 0 && freeIdx < src->head.size) {
        EXPECT_TRUE(raw->arr[freeIdx].isdeleted);
    }

    munmap(mapped, sizeof(treefile_serializable));
    close(fd);
    delete src;
}

// Test 15: UnmountRemountNodeArrayIntegrity
// The core test: save (unmount), destroy, load (remount), memcmp the ENTIRE
// 100K-entry node array to prove it comes back byte-for-byte identical.
TEST_F(PersistenceTest, UnmountRemountNodeArrayIntegrity) {
    // --- MOUNT: build a filesystem ---
    treefile* mounted = new treefile();
    initialize(*mounted);

    insertfolder("home", "/", *mounted);
    insertfolder("user", "home", *mounted);
    insertfile(".bashrc", "user", *mounted);
    insertfile(".profile", "user", *mounted);
    insertfolder("projects", "user", *mounted);
    insertfile("main.cpp", "projects", *mounted);
    insertfolder("docs", "/", *mounted);
    insertfile("README.md", "docs", *mounted);

    // Snapshot the entire node array before unmount
    treenode* snapshot = new treenode[100000];
    memcpy(snapshot, mounted->arr, sizeof(mounted->arr));
    int savedAllocated = mounted->head.nodeallocated;
    int savedFirstFree = mounted->head.firstfree;

    // --- UNMOUNT ---
    ASSERT_TRUE(save_treefile(test_file, *mounted));
    delete mounted;
    mounted = nullptr;

    // --- REMOUNT (fresh process) ---
    treefile* remounted = new treefile();
    initialize(*remounted);
    ASSERT_TRUE(load_treefile(test_file, *remounted));

    // Header must match
    EXPECT_EQ(remounted->head.nodeallocated, savedAllocated);
    EXPECT_EQ(remounted->head.firstfree, savedFirstFree);

    // The ENTIRE node array must be byte-for-byte identical
    EXPECT_EQ(memcmp(remounted->arr, snapshot, sizeof(remounted->arr)), 0)
        << "Node array mismatch after unmount/remount — data did not persist!";

    // Sanity-check lookups still work on the remounted tree
    EXPECT_TRUE(remounted->head.hash.has("/"));
    EXPECT_TRUE(remounted->head.hash.has("home"));
    EXPECT_TRUE(remounted->head.hash.has("main.cpp"));
    EXPECT_TRUE(remounted->head.hash.has("README.md"));

    delete[] snapshot;
    delete remounted;
}

// Test 16: UnmountRemountHashMapIntegrity
// Verify the hash map roundtrips exactly through mmap persistence
TEST_F(PersistenceTest, UnmountRemountHashMapIntegrity) {
    treefile* mounted = new treefile();
    initialize(*mounted);

    for (int i = 0; i < 200; i++) {
        insertfile("hm_" + std::to_string(i), "/", *mounted);
    }

    // Snapshot the raw hashmap_t
    hashmap_t* hashSnap = (hashmap_t*)malloc(sizeof(hashmap_t));
    ASSERT_NE(hashSnap, nullptr);
    memcpy(hashSnap, mounted->head.hash.m, sizeof(hashmap_t));

    // UNMOUNT
    ASSERT_TRUE(save_treefile(test_file, *mounted));
    delete mounted;

    // REMOUNT
    treefile* remounted = new treefile();
    initialize(*remounted);
    ASSERT_TRUE(load_treefile(test_file, *remounted));

    // Entire hash map must be byte-for-byte identical
    EXPECT_EQ(memcmp(remounted->head.hash.m, hashSnap, sizeof(hashmap_t)), 0)
        << "Hash map mismatch after unmount/remount!";

    // All entries accessible
    for (int i = 0; i < 200; i++) {
        std::string name = "hm_" + std::to_string(i);
        EXPECT_TRUE(remounted->head.hash.has(name)) << "Missing: " << name;
    }

    free(hashSnap);
    delete remounted;
}

// Test 17: UnmountRemountFullCycle
// Realistic lifecycle: dirs, files, metadata, deletions, then unmount/remount
TEST_F(PersistenceTest, UnmountRemountFullCycle) {
    // --- MOUNT & BUILD ---
    treefile* fs = new treefile();
    initialize(*fs);

    insertfolder("src", "/", *fs);
    insertfolder("include", "/", *fs);
    insertfolder("build", "/", *fs);
    insertfolder("test", "/", *fs);

    insertfile("src/main.cpp", "src", *fs);
    insertfile("src/utils.cpp", "src", *fs);
    insertfile("include/utils.h", "include", *fs);
    insertfile("test/test_main.cpp", "test", *fs);

    // Set specific metadata
    int mainIdx = hashindex("src/main.cpp", *fs);
    ASSERT_NE(mainIdx, -1);
    fs->arr[mainIdx].metadata.size = 2048;
    fs->arr[mainIdx].metadata.inode = 100;
    fs->arr[mainIdx].metadata.atime = 1700000000;
    fs->arr[mainIdx].metadata.mtime = 1700000001;
    fs->arr[mainIdx].metadata.ctime = 1700000002;

    // Delete a file (creates free-list entry)
    delete1("src/utils.cpp", *fs);

    int allocBefore = fs->head.nodeallocated;
    int firstFreeBefore = fs->head.firstfree;

    // --- UNMOUNT ---
    ASSERT_TRUE(save_treefile(test_file, *fs));
    delete fs;

    // --- REMOUNT ---
    fs = new treefile();
    initialize(*fs);
    ASSERT_TRUE(load_treefile(test_file, *fs));

    // Header state
    EXPECT_EQ(fs->head.nodeallocated, allocBefore);
    EXPECT_EQ(fs->head.firstfree, firstFreeBefore);

    // Directories exist and are directories
    for (const char* dir : {"src", "include", "build", "test"}) {
        EXPECT_TRUE(fs->head.hash.has(dir)) << "Missing dir: " << dir;
        int idx = hashindex(std::string(dir), *fs);
        EXPECT_NE(idx, -1);
        EXPECT_TRUE(S_ISDIR(fs->arr[idx].metadata.mode)) << dir << " should be a directory";
    }

    // Existing files present
    EXPECT_TRUE(fs->head.hash.has("src/main.cpp"));
    EXPECT_TRUE(fs->head.hash.has("include/utils.h"));
    EXPECT_TRUE(fs->head.hash.has("test/test_main.cpp"));

    // Deleted file stays deleted
    EXPECT_FALSE(fs->head.hash.has("src/utils.cpp"));

    // Metadata persisted
    int loadedMainIdx = hashindex("src/main.cpp", *fs);
    ASSERT_NE(loadedMainIdx, -1);
    EXPECT_EQ(fs->arr[loadedMainIdx].metadata.size, 2048);
    EXPECT_EQ(fs->arr[loadedMainIdx].metadata.inode, 100);
    EXPECT_EQ(fs->arr[loadedMainIdx].metadata.atime, 1700000000);
    EXPECT_EQ(fs->arr[loadedMainIdx].metadata.mtime, 1700000001);
    EXPECT_EQ(fs->arr[loadedMainIdx].metadata.ctime, 1700000002);

    // Parent-child relationship intact
    int srcIdx = hashindex("src", *fs);
    EXPECT_EQ(fs->arr[loadedMainIdx].parent, srcIdx);

    // Free list works — can still allocate
    insertfile("new_after_remount.txt", "/", *fs);
    EXPECT_TRUE(fs->head.hash.has("new_after_remount.txt"));

    delete fs;
}

// Test 18: MultipleUnmountRemountCycles
// Mount → modify → unmount → remount → modify → unmount → remount
// Verifies cumulative state across several restart cycles.
TEST_F(PersistenceTest, MultipleUnmountRemountCycles) {
    // --- Cycle 1: initial structure ---
    {
        treefile* fs = new treefile();
        initialize(*fs);
        insertfolder("cycle1_dir", "/", *fs);
        insertfile("cycle1_file", "cycle1_dir", *fs);
        ASSERT_TRUE(save_treefile(test_file, *fs));
        delete fs;
    }

    // --- Cycle 2: load, add more, save ---
    {
        treefile* fs = new treefile();
        initialize(*fs);
        ASSERT_TRUE(load_treefile(test_file, *fs));

        EXPECT_TRUE(fs->head.hash.has("cycle1_dir"));
        EXPECT_TRUE(fs->head.hash.has("cycle1_file"));

        insertfolder("cycle2_dir", "/", *fs);
        insertfile("cycle2_file", "cycle2_dir", *fs);

        ASSERT_TRUE(save_treefile(test_file, *fs));
        delete fs;
    }

    // --- Cycle 3: load, delete some, add more, save ---
    {
        treefile* fs = new treefile();
        initialize(*fs);
        ASSERT_TRUE(load_treefile(test_file, *fs));

        EXPECT_TRUE(fs->head.hash.has("cycle1_dir"));
        EXPECT_TRUE(fs->head.hash.has("cycle2_dir"));

        delete1("cycle1_file", *fs);
        insertfile("cycle3_file", "cycle1_dir", *fs);

        ASSERT_TRUE(save_treefile(test_file, *fs));
        delete fs;
    }

    // --- Final verification ---
    {
        treefile* fs = new treefile();
        initialize(*fs);
        ASSERT_TRUE(load_treefile(test_file, *fs));

        // Cycle 1 dir survives, but cycle 1 file was deleted in cycle 3
        EXPECT_TRUE(fs->head.hash.has("cycle1_dir"));
        EXPECT_FALSE(fs->head.hash.has("cycle1_file"));

        // Cycle 2 fully intact
        EXPECT_TRUE(fs->head.hash.has("cycle2_dir"));
        EXPECT_TRUE(fs->head.hash.has("cycle2_file"));

        // Cycle 3 file present and parented correctly
        EXPECT_TRUE(fs->head.hash.has("cycle3_file"));
        int c3fIdx = hashindex("cycle3_file", *fs);
        int c1dIdx = hashindex("cycle1_dir", *fs);
        EXPECT_EQ(fs->arr[c3fIdx].parent, c1dIdx);

        delete fs;
    }
}

// Test 19: MmapOnDiskMatchesLoadedState
// After save, mmap the file AND load via load_treefile — compare both views.
// Proves load_treefile faithfully reconstructs what mmap wrote to disk.
TEST_F(PersistenceTest, MmapOnDiskMatchesLoadedState) {
    treefile* src = new treefile();
    initialize(*src);

    for (int i = 0; i < 50; i++) {
        insertfile("verify_" + std::to_string(i), "/", *src);
    }

    ASSERT_TRUE(save_treefile(test_file, *src));
    delete src;

    // Load via load_treefile
    treefile* loaded = new treefile();
    initialize(*loaded);
    ASSERT_TRUE(load_treefile(test_file, *loaded));

    // Also mmap the file directly
    int fd = open(test_file, O_RDONLY);
    ASSERT_NE(fd, -1);

    void* mapped = mmap(NULL, sizeof(treefile_serializable), PROT_READ, MAP_PRIVATE, fd, 0);
    ASSERT_NE(mapped, MAP_FAILED);

    const treefile_serializable* raw = static_cast<const treefile_serializable*>(mapped);

    // Node array: loaded state must equal raw on-disk bytes
    EXPECT_EQ(memcmp(loaded->arr, raw->arr, sizeof(loaded->arr)), 0)
        << "Node array: load_treefile result differs from raw mmap!";

    // Hash map: loaded state must equal raw on-disk bytes
    EXPECT_EQ(memcmp(loaded->head.hash.m, &raw->head.hashdata, sizeof(hashmap_t)), 0)
        << "Hash map: load_treefile result differs from raw mmap!";

    // Header scalar fields
    EXPECT_EQ(loaded->head.firstfree, raw->head.firstfree);
    EXPECT_EQ(loaded->head.start, raw->head.start);
    EXPECT_EQ(loaded->head.size, raw->head.size);
    EXPECT_EQ(loaded->head.nodeallocated, raw->head.nodeallocated);

    munmap(mapped, sizeof(treefile_serializable));
    close(fd);
    delete loaded;
}

// ============================================================
// Additional mmap persistence tests (20–39)
// ============================================================

// Helper: mmap the test file read-only and return the pointer.
// Caller must munmap + close fd.
static const treefile_serializable* mmap_open_readonly(const char* path, int &fd_out) {
    fd_out = open(path, O_RDONLY);
    if (fd_out == -1) return nullptr;
    void* mapped = mmap(NULL, sizeof(treefile_serializable), PROT_READ, MAP_PRIVATE, fd_out, 0);
    if (mapped == MAP_FAILED) { close(fd_out); return nullptr; }
    return static_cast<const treefile_serializable*>(mapped);
}

static void mmap_close(const treefile_serializable* raw, int fd) {
    munmap(const_cast<treefile_serializable*>(raw), sizeof(treefile_serializable));
    close(fd);
}

// Test 20: MmapSiblingChainPersistence
// Build 5 children under one parent, verify the sibling linked-list on disk.
TEST_F(PersistenceTest, MmapSiblingChainPersistence) {
    treefile* fs = new treefile();
    initialize(*fs);

    insertfolder("parent_dir", "/", *fs);
    for (int i = 0; i < 5; i++)
        insertfile("sib_" + std::to_string(i), "parent_dir", *fs);

    int parentIdx = hashindex("parent_dir", *fs);
    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    // Walk the sibling chain on disk and count children
    int count = 0;
    int cur = raw->arr[parentIdx].firstchild;
    while (cur != -1 && cur < 100000 && count < 100) {
        EXPECT_FALSE(raw->arr[cur].isdeleted);
        EXPECT_EQ(raw->arr[cur].parent, parentIdx);
        cur = raw->arr[cur].nextsibling;
        count++;
    }
    EXPECT_EQ(count, 5);

    mmap_close(raw, fd);
    delete fs;
}

// Test 21: MmapDeletedNodesMarkedOnDisk
// Delete nodes, save, mmap and verify they are isdeleted=true with cleared names.
TEST_F(PersistenceTest, MmapDeletedNodesMarkedOnDisk) {
    treefile* fs = new treefile();
    initialize(*fs);

    insertfile("alive", "/", *fs);
    insertfile("dead1", "/", *fs);
    insertfile("dead2", "/", *fs);

    int dead1Idx = hashindex("dead1", *fs);
    int dead2Idx = hashindex("dead2", *fs);
    int aliveIdx = hashindex("alive", *fs);

    delete1("dead1", *fs);
    delete1("dead2", *fs);

    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    EXPECT_TRUE(raw->arr[dead1Idx].isdeleted);
    EXPECT_TRUE(raw->arr[dead2Idx].isdeleted);
    EXPECT_EQ(raw->arr[dead1Idx].metadata.name[0], '\0');
    EXPECT_EQ(raw->arr[dead2Idx].metadata.name[0], '\0');

    EXPECT_FALSE(raw->arr[aliveIdx].isdeleted);
    EXPECT_STREQ(raw->arr[aliveIdx].metadata.name, "alive");

    mmap_close(raw, fd);
    delete fs;
}

// Test 22: MmapFreeListChainOnDisk
// Walk the free list via raw mmap and verify it forms a valid chain.
TEST_F(PersistenceTest, MmapFreeListChainOnDisk) {
    treefile* fs = new treefile();
    initialize(*fs);

    for (int i = 0; i < 20; i++)
        insertfile("fl_" + std::to_string(i), "/", *fs);
    for (int i = 0; i < 10; i++)
        delete1("fl_" + std::to_string(i), *fs);

    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    // Walk the free list from the header
    int freeCount = 0;
    int cur = raw->head.firstfree;
    while (cur != -1 && cur < 100000 && freeCount < 200000) {
        EXPECT_TRUE(raw->arr[cur].isdeleted)
            << "Free list node " << cur << " is not marked deleted!";
        cur = raw->arr[cur].nextfree;
        freeCount++;
    }
    EXPECT_GT(freeCount, 0);

    mmap_close(raw, fd);
    delete fs;
}

// Test 23: MmapRootNodePersistence
// Verify root node (index 0) fields survive on disk exactly.
TEST_F(PersistenceTest, MmapRootNodePersistence) {
    treefile* fs = new treefile();
    initialize(*fs);

    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    EXPECT_FALSE(raw->arr[0].isdeleted);
    EXPECT_STREQ(raw->arr[0].metadata.name, "/");
    EXPECT_TRUE(S_ISDIR(raw->arr[0].metadata.mode));
    EXPECT_EQ(raw->arr[0].metadata.nlink, 2u);
    EXPECT_EQ(raw->arr[0].parent, -1);
    EXPECT_EQ(raw->arr[0].metadata.inode, 1);

    mmap_close(raw, fd);
    delete fs;
}

// Test 24: MmapNodeArrayPartialComparison
// Save, mmap, then compare just the first N allocated nodes byte-by-byte.
TEST_F(PersistenceTest, MmapNodeArrayPartialComparison) {
    treefile* fs = new treefile();
    initialize(*fs);

    for (int i = 0; i < 100; i++)
        insertfile("partial_" + std::to_string(i), "/", *fs);

    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    // Check each allocated node individually
    for (int i = 0; i < 101; i++) {  // root + 100 files
        EXPECT_EQ(memcmp(&fs->arr[i], &raw->arr[i], sizeof(treenode)), 0)
            << "Node " << i << " differs between memory and disk";
    }

    mmap_close(raw, fd);
    delete fs;
}

// Test 25: MmapOverwriteSavePersistence
// Save twice to the same file, verify the second save overwrites cleanly.
TEST_F(PersistenceTest, MmapOverwriteSavePersistence) {
    treefile* fs = new treefile();
    initialize(*fs);

    insertfile("first_save_file", "/", *fs);
    ASSERT_TRUE(save_treefile(test_file, *fs));

    // Modify and save again
    delete1("first_save_file", *fs);
    insertfolder("second_save_dir", "/", *fs);
    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    // second_save_dir should exist on disk
    int dirIdx = hashindex("second_save_dir", *fs);
    ASSERT_NE(dirIdx, -1);
    EXPECT_FALSE(raw->arr[dirIdx].isdeleted);
    EXPECT_STREQ(raw->arr[dirIdx].metadata.name, "second_save_dir");

    // first_save_file should be deleted on disk
    // (its index was recycled or still marked deleted)
    EXPECT_FALSE(fs->head.hash.has("first_save_file"));

    mmap_close(raw, fd);
    delete fs;
}

// Test 26: MmapHashmapSizeOnDisk
// Verify the hashmap size field on disk matches in-memory count.
TEST_F(PersistenceTest, MmapHashmapSizeOnDisk) {
    treefile* fs = new treefile();
    initialize(*fs);

    for (int i = 0; i < 75; i++)
        insertfile("hs_" + std::to_string(i), "/", *fs);

    size_t expectedSize = fs->head.hash.size();
    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    EXPECT_EQ(raw->head.hashdata.size, expectedSize);

    mmap_close(raw, fd);
    delete fs;
}

// Test 27: MmapDeepTreeParentChainOnDisk
// Create deep tree, mmap, walk parent chain from leaf to root entirely on disk.
TEST_F(PersistenceTest, MmapDeepTreeParentChainOnDisk) {
    treefile* fs = new treefile();
    initialize(*fs);

    std::string parent = "/";
    std::vector<int> indices;
    for (int i = 0; i < 15; i++) {
        std::string name = "deep_" + std::to_string(i);
        insertfolder(name, parent, *fs);
        indices.push_back(hashindex(name, *fs));
        parent = name;
    }

    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    // Walk parent chain on disk from deepest node to root
    int cur = indices.back();
    int depth = 0;
    while (cur > 0 && depth < 50) {
        EXPECT_FALSE(raw->arr[cur].isdeleted);
        cur = raw->arr[cur].parent;
        depth++;
    }
    EXPECT_EQ(cur, 0);     // reached root
    EXPECT_EQ(depth, 15);  // correct depth

    mmap_close(raw, fd);
    delete fs;
}

// Test 28: MmapFileModeBitsOnDisk
// Verify mode bits for files vs directories are correct on disk.
TEST_F(PersistenceTest, MmapFileModeBitsOnDisk) {
    treefile* fs = new treefile();
    initialize(*fs);

    insertfolder("mode_dir", "/", *fs);
    insertfile("mode_file", "/", *fs);

    int dirIdx = hashindex("mode_dir", *fs);
    int fileIdx = hashindex("mode_file", *fs);

    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    EXPECT_TRUE(S_ISDIR(raw->arr[dirIdx].metadata.mode));
    EXPECT_TRUE(S_ISREG(raw->arr[fileIdx].metadata.mode));

    // Check permission bits
    EXPECT_NE(raw->arr[dirIdx].metadata.mode & 0755, 0u);
    EXPECT_NE(raw->arr[fileIdx].metadata.mode & 0644, 0u);

    mmap_close(raw, fd);
    delete fs;
}

// Test 29: MmapTimestampsOnDisk
// Set specific timestamps, save, verify on disk via mmap.
TEST_F(PersistenceTest, MmapTimestampsOnDisk) {
    treefile* fs = new treefile();
    initialize(*fs);

    insertfile("ts_file", "/", *fs);
    int idx = hashindex("ts_file", *fs);
    ASSERT_NE(idx, -1);

    fs->arr[idx].metadata.atime = 1234567890;
    fs->arr[idx].metadata.mtime = 1234567891;
    fs->arr[idx].metadata.ctime = 1234567892;

    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    EXPECT_EQ(raw->arr[idx].metadata.atime, 1234567890);
    EXPECT_EQ(raw->arr[idx].metadata.mtime, 1234567891);
    EXPECT_EQ(raw->arr[idx].metadata.ctime, 1234567892);

    mmap_close(raw, fd);
    delete fs;
}

// Test 30: MmapUidGidOnDisk
// Verify uid/gid persist on disk.
TEST_F(PersistenceTest, MmapUidGidOnDisk) {
    treefile* fs = new treefile();
    initialize(*fs);

    insertfile("ug_file", "/", *fs);
    int idx = hashindex("ug_file", *fs);
    ASSERT_NE(idx, -1);

    uid_t expectedUid = fs->arr[idx].metadata.uid;
    gid_t expectedGid = fs->arr[idx].metadata.gid;

    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    EXPECT_EQ(raw->arr[idx].metadata.uid, expectedUid);
    EXPECT_EQ(raw->arr[idx].metadata.gid, expectedGid);

    mmap_close(raw, fd);
    delete fs;
}

// Test 31: MmapNlinkOnDisk
// Verify nlink counts persist for dirs and files.
TEST_F(PersistenceTest, MmapNlinkOnDisk) {
    treefile* fs = new treefile();
    initialize(*fs);

    insertfolder("nlink_dir", "/", *fs);
    insertfile("nlink_file", "nlink_dir", *fs);

    int dirIdx = hashindex("nlink_dir", *fs);
    int fileIdx = hashindex("nlink_file", *fs);

    nlink_t dirNlink = fs->arr[dirIdx].metadata.nlink;
    nlink_t fileNlink = fs->arr[fileIdx].metadata.nlink;

    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    EXPECT_EQ(raw->arr[dirIdx].metadata.nlink, dirNlink);
    EXPECT_EQ(raw->arr[fileIdx].metadata.nlink, fileNlink);

    mmap_close(raw, fd);
    delete fs;
}

// Test 32: MmapFileSizeOnDisk
// Set file sizes, verify they survive on disk.
TEST_F(PersistenceTest, MmapFileSizeOnDisk) {
    treefile* fs = new treefile();
    initialize(*fs);

    insertfile("big_file", "/", *fs);
    insertfile("small_file", "/", *fs);

    int bigIdx = hashindex("big_file", *fs);
    int smallIdx = hashindex("small_file", *fs);
    fs->arr[bigIdx].metadata.size = 1048576;  // 1 MB
    fs->arr[smallIdx].metadata.size = 42;

    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    EXPECT_EQ(raw->arr[bigIdx].metadata.size, 1048576);
    EXPECT_EQ(raw->arr[smallIdx].metadata.size, 42);

    mmap_close(raw, fd);
    delete fs;
}

// Test 33: MmapRemountThenMutateAndVerify
// Remount, insert new nodes, save again, mmap-verify the new nodes on disk.
TEST_F(PersistenceTest, MmapRemountThenMutateAndVerify) {
    // Mount 1
    {
        treefile* fs = new treefile();
        initialize(*fs);
        insertfolder("base_dir", "/", *fs);
        ASSERT_TRUE(save_treefile(test_file, *fs));
        delete fs;
    }

    // Remount, mutate, save
    {
        treefile* fs = new treefile();
        initialize(*fs);
        ASSERT_TRUE(load_treefile(test_file, *fs));

        insertfile("new_after_mount", "base_dir", *fs);
        fs->arr[hashindex("new_after_mount", *fs)].metadata.inode = 9999;

        ASSERT_TRUE(save_treefile(test_file, *fs));
        delete fs;
    }

    // Verify new node on disk via mmap
    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    // Find the node — search on disk for the name
    bool found = false;
    for (int i = 0; i < 100000; i++) {
        if (!raw->arr[i].isdeleted &&
            strcmp(raw->arr[i].metadata.name, "new_after_mount") == 0) {
            EXPECT_EQ(raw->arr[i].metadata.inode, 9999);
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "new_after_mount not found on disk";

    mmap_close(raw, fd);
}

// Test 34: MmapHashEntryLookupOnDisk
// Manually probe the on-disk hash table for a known key.
TEST_F(PersistenceTest, MmapHashEntryLookupOnDisk) {
    treefile* fs = new treefile();
    initialize(*fs);

    insertfile("hash_probe_target", "/", *fs);
    int expectedIdx = hashindex("hash_probe_target", *fs);
    ASSERT_NE(expectedIdx, -1);

    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    // Manually search the on-disk hash table for our key
    bool found = false;
    for (size_t i = 0; i < MAX_HASH_ENTRIES; i++) {
        if (raw->head.hashdata.entries[i].occupied &&
            strcmp(raw->head.hashdata.entries[i].key, "hash_probe_target") == 0) {
            EXPECT_EQ(raw->head.hashdata.entries[i].value, expectedIdx);
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "hash_probe_target not found in on-disk hash table";

    mmap_close(raw, fd);
    delete fs;
}

// Test 35: MmapDeletedNodeRecycledAfterRemount
// Delete a node, save, remount, insert new node (reuses slot), save again,
// mmap-verify the slot now holds the new node's data.
TEST_F(PersistenceTest, MmapDeletedNodeRecycledAfterRemount) {
    int recycledSlot = -1;

    // Mount 1: create and delete
    {
        treefile* fs = new treefile();
        initialize(*fs);
        insertfile("ephemeral", "/", *fs);
        recycledSlot = hashindex("ephemeral", *fs);
        delete1("ephemeral", *fs);
        ASSERT_TRUE(save_treefile(test_file, *fs));
        delete fs;
    }

    // Mount 2: load, insert new (should reuse the freed slot)
    {
        treefile* fs = new treefile();
        initialize(*fs);
        ASSERT_TRUE(load_treefile(test_file, *fs));

        insertfile("reborn", "/", *fs);
        int rebornIdx = hashindex("reborn", *fs);
        // The freed slot should be the one reused (it was at head of free list)
        EXPECT_EQ(rebornIdx, recycledSlot);

        ASSERT_TRUE(save_treefile(test_file, *fs));
        delete fs;
    }

    // Verify on disk
    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    EXPECT_FALSE(raw->arr[recycledSlot].isdeleted);
    EXPECT_STREQ(raw->arr[recycledSlot].metadata.name, "reborn");

    mmap_close(raw, fd);
}

// Test 36: MmapChangeParentPersistence
// Move a file from one dir to another, save, verify parent pointer on disk.
TEST_F(PersistenceTest, MmapChangeParentPersistence) {
    treefile* fs = new treefile();
    initialize(*fs);

    insertfolder("dir_a", "/", *fs);
    insertfolder("dir_b", "/", *fs);
    insertfile("movable", "dir_a", *fs);

    change_parent("movable", "dir_b", *fs);

    int movIdx = hashindex("movable", *fs);
    int dirBIdx = hashindex("dir_b", *fs);

    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    EXPECT_EQ(raw->arr[movIdx].parent, dirBIdx);
    EXPECT_STREQ(raw->arr[movIdx].metadata.name, "movable");

    mmap_close(raw, fd);
    delete fs;
}

// Test 37: MmapEmptyFsOnDisk
// Initialize an empty filesystem (just root), save, verify disk image.
TEST_F(PersistenceTest, MmapEmptyFsOnDisk) {
    treefile* fs = new treefile();
    initialize(*fs);

    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    // Only root exists
    EXPECT_EQ(raw->head.nodeallocated, 1);
    EXPECT_EQ(raw->head.firstfree, 1);
    EXPECT_FALSE(raw->arr[0].isdeleted);

    // All other nodes should be in free list (deleted)
    for (int i = 1; i < 100; i++) {
        EXPECT_TRUE(raw->arr[i].isdeleted)
            << "Node " << i << " should be free in empty filesystem";
    }

    mmap_close(raw, fd);
    delete fs;
}

// Test 38: MmapSubtreeDeletionOnDisk
// Create a subtree, delete it, save, verify entire subtree is deleted on disk.
TEST_F(PersistenceTest, MmapSubtreeDeletionOnDisk) {
    treefile* fs = new treefile();
    initialize(*fs);

    insertfolder("root_dir", "/", *fs);
    insertfolder("sub1", "root_dir", *fs);
    insertfile("sub1_f1", "sub1", *fs);
    insertfile("sub1_f2", "sub1", *fs);
    insertfolder("sub2", "root_dir", *fs);
    insertfile("sub2_f1", "sub2", *fs);

    int sub1Idx = hashindex("sub1", *fs);
    int f1Idx = hashindex("sub1_f1", *fs);
    int f2Idx = hashindex("sub1_f2", *fs);
    int sub2Idx = hashindex("sub2", *fs);
    int sub2f1Idx = hashindex("sub2_f1", *fs);
    int rootDirIdx = hashindex("root_dir", *fs);

    // Delete the whole root_dir subtree
    delete1("root_dir", *fs);

    ASSERT_TRUE(save_treefile(test_file, *fs));

    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    // All nodes in the subtree must be deleted on disk
    EXPECT_TRUE(raw->arr[rootDirIdx].isdeleted);
    EXPECT_TRUE(raw->arr[sub1Idx].isdeleted);
    EXPECT_TRUE(raw->arr[f1Idx].isdeleted);
    EXPECT_TRUE(raw->arr[f2Idx].isdeleted);
    EXPECT_TRUE(raw->arr[sub2Idx].isdeleted);
    EXPECT_TRUE(raw->arr[sub2f1Idx].isdeleted);

    // Root (index 0) still alive
    EXPECT_FALSE(raw->arr[0].isdeleted);

    mmap_close(raw, fd);
    delete fs;
}

// Test 39: MmapBulkInsertDeleteRemount
// Bulk insert 500, delete 250, save, remount, mmap-verify counts.
TEST_F(PersistenceTest, MmapBulkInsertDeleteRemount) {
    int allocAfterDelete = 0;
    int firstFreeAfterDelete = 0;

    {
        treefile* fs = new treefile();
        initialize(*fs);

        for (int i = 0; i < 500; i++)
            insertfile("bulk_" + std::to_string(i), "/", *fs);

        for (int i = 0; i < 250; i++)
            delete1("bulk_" + std::to_string(i), *fs);

        allocAfterDelete = fs->head.nodeallocated;
        firstFreeAfterDelete = fs->head.firstfree;

        ASSERT_TRUE(save_treefile(test_file, *fs));
        delete fs;
    }

    // Remount
    treefile* fs = new treefile();
    initialize(*fs);
    ASSERT_TRUE(load_treefile(test_file, *fs));

    // mmap the file
    int fd;
    const treefile_serializable* raw = mmap_open_readonly(test_file, fd);
    ASSERT_NE(raw, nullptr);

    // Header matches
    EXPECT_EQ(raw->head.nodeallocated, allocAfterDelete);
    EXPECT_EQ(raw->head.firstfree, firstFreeAfterDelete);

    // On disk: surviving files are alive
    for (int i = 250; i < 500; i++) {
        std::string name = "bulk_" + std::to_string(i);
        int idx = hashindex(name, *fs);
        ASSERT_NE(idx, -1) << "Missing after remount: " << name;
        EXPECT_FALSE(raw->arr[idx].isdeleted);
        EXPECT_STREQ(raw->arr[idx].metadata.name, name.c_str());
    }

    // On disk: deleted files are gone from hash
    for (int i = 0; i < 250; i++) {
        std::string name = "bulk_" + std::to_string(i);
        EXPECT_FALSE(fs->head.hash.has(name));
    }

    // Node array byte-for-byte identical between load and disk
    EXPECT_EQ(memcmp(fs->arr, raw->arr, sizeof(fs->arr)), 0);

    mmap_close(raw, fd);
    delete fs;
}
