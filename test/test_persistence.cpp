#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <vector>
#include <atomic>
#include "../include/daemon/directory tree/adt.h"

class PersistenceTest : public ::testing::Test {
protected:
    const char* test_file = "/tmp/test_treefile.mmap";

    void SetUp() override {
        unlink(test_file);
    }

    void TearDown() override {
        unlink(test_file);
    }
};

// ==========================================================================
// Basic mmap persistence tests
// ==========================================================================

// Test 1: MmapCreateAndOpen
// Verify that mmap_init_treefile creates a new file and initializes it
TEST_F(PersistenceTest, MmapCreateAndOpen) {
    treefile* ptr = nullptr;
    int fd = -1;
    size_t mapsize = 0;

    ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));
    ASSERT_NE(ptr, nullptr);

    // File should exist on disk with correct size
    struct stat st;
    ASSERT_EQ(stat(test_file, &st), 0);
    EXPECT_EQ((size_t)st.st_size, sizeof(treefile));

    // Tree should be initialized
    EXPECT_EQ(ptr->size, TREEFILE_MAX_NODES);
    EXPECT_EQ(ptr->nodeallocated, 1);
    EXPECT_EQ(ptr->firstfree, -1);
    EXPECT_TRUE(hashmap_has(&ptr->hashdata, "/"));

    mmap_close_treefile(ptr, fd, mapsize);
}

// Test 2: PersistenceAcrossRestarts
// Create tree, close (unmount), reopen (remount), verify data persists
TEST_F(PersistenceTest, PersistenceAcrossRestarts) {
    int savedNodeCount = 0;

    // First "run" — create and populate
    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        insertfolder("projects", "/", *ptr);
        insertfolder("src", "projects", *ptr);
        insertfile("main.cpp", "src", *ptr);
        insertfile("readme.md", "projects", *ptr);

        savedNodeCount = ptr->nodeallocated;
        mmap_close_treefile(ptr, fd, mapsize);
    }

    // Second "run" — load and verify
    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "projects"));
        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "src"));
        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "main.cpp"));
        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "readme.md"));
        EXPECT_EQ(ptr->nodeallocated, savedNodeCount);

        // Can continue operations after reload
        insertfile("new_after_load.txt", "projects", *ptr);
        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "new_after_load.txt"));

        mmap_close_treefile(ptr, fd, mapsize);
    }
}

// Test 3: UnmountRemountNodeIntegrity
// Build a filesystem tree, unmount, remount, verify byte-level node integrity
TEST_F(PersistenceTest, UnmountRemountNodeIntegrity) {
    int mainIdx_saved = -1;

    // MOUNT and build
    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        insertfolder("home", "/", *ptr);
        insertfolder("user", "home", *ptr);
        insertfile(".bashrc", "user", *ptr);
        insertfile(".profile", "user", *ptr);
        insertfolder("projects", "user", *ptr);
        insertfile("main.cpp", "projects", *ptr);

        mainIdx_saved = hashmap_get(&ptr->hashdata, "main.cpp");
        ptr->arr[mainIdx_saved].metadata.size = 2048;
        ptr->arr[mainIdx_saved].metadata.inode = 100;
        ptr->arr[mainIdx_saved].metadata.atime = 1700000000;

        mmap_close_treefile(ptr, fd, mapsize);
    }

    // REMOUNT and verify
    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "home"));
        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "main.cpp"));

        int mainIdx = hashmap_get(&ptr->hashdata, "main.cpp");
        EXPECT_EQ(mainIdx, mainIdx_saved);
        EXPECT_EQ(ptr->arr[mainIdx].metadata.size, 2048);
        EXPECT_EQ(ptr->arr[mainIdx].metadata.inode, 100);
        EXPECT_EQ(ptr->arr[mainIdx].metadata.atime, 1700000000);

        // Parent chain intact
        int userIdx = hashmap_get(&ptr->hashdata, "user");
        EXPECT_EQ(ptr->arr[mainIdx_saved].parent,
                  hashmap_get(&ptr->hashdata, "projects"));

        mmap_close_treefile(ptr, fd, mapsize);
    }
}

// Test 4: MultipleRestartCycles
// Mount → modify → unmount → remount → modify → unmount → remount
TEST_F(PersistenceTest, MultipleRestartCycles) {
    // Cycle 1
    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));
        insertfolder("cycle1_dir", "/", *ptr);
        insertfile("cycle1_file", "cycle1_dir", *ptr);
        mmap_close_treefile(ptr, fd, mapsize);
    }

    // Cycle 2
    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));
        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "cycle1_dir"));
        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "cycle1_file"));
        insertfolder("cycle2_dir", "/", *ptr);
        insertfile("cycle2_file", "cycle2_dir", *ptr);
        mmap_close_treefile(ptr, fd, mapsize);
    }

    // Cycle 3: delete from cycle 1, add new
    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));
        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "cycle1_dir"));
        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "cycle2_dir"));
        delete1("cycle1_file", *ptr);
        insertfile("cycle3_file", "cycle1_dir", *ptr);
        mmap_close_treefile(ptr, fd, mapsize);
    }

    // Final verification
    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "cycle1_dir"));
        EXPECT_FALSE(hashmap_has(&ptr->hashdata, "cycle1_file")); // deleted
        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "cycle2_dir"));
        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "cycle2_file"));
        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "cycle3_file"));

        int c3fIdx = hashmap_get(&ptr->hashdata, "cycle3_file");
        int c1dIdx = hashmap_get(&ptr->hashdata, "cycle1_dir");
        EXPECT_EQ(ptr->arr[c3fIdx].parent, c1dIdx);

        mmap_close_treefile(ptr, fd, mapsize);
    }
}

// Test 5: LoadNonExistentCreatesNew
// init_or_load on a non-existent path should create a fresh tree
TEST_F(PersistenceTest, LoadNonExistentCreatesNew) {
    treefile* ptr = nullptr;
    int fd = -1;
    size_t mapsize = 0;

    ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));
    EXPECT_EQ(ptr->nodeallocated, 1);
    EXPECT_TRUE(hashmap_has(&ptr->hashdata, "/"));

    struct stat st;
    EXPECT_EQ(stat(test_file, &st), 0);

    mmap_close_treefile(ptr, fd, mapsize);
}

// Test 6: DeepTreePersistence
TEST_F(PersistenceTest, DeepTreePersistence) {
    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        std::string parent = "/";
        for (int i = 0; i < 10; i++) {
            std::string name = "dir" + std::to_string(i);
            insertfolder(name, parent, *ptr);
            parent = name;
        }
        mmap_close_treefile(ptr, fd, mapsize);
    }

    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        for (int i = 0; i < 10; i++) {
            std::string name = "dir" + std::to_string(i);
            EXPECT_TRUE(hashmap_has(&ptr->hashdata, name.c_str()));
            int idx = hashmap_get(&ptr->hashdata, name.c_str());
            EXPECT_FALSE(ptr->arr[idx].isdeleted);
        }

        // Verify parent chain: dir9 -> dir8 -> ... -> dir0 -> root
        int idx = hashmap_get(&ptr->hashdata, "dir9");
        int depth = 0;
        while (idx > 0 && depth < 20) {
            idx = ptr->arr[idx].parent;
            depth++;
        }
        EXPECT_EQ(depth, 10);

        mmap_close_treefile(ptr, fd, mapsize);
    }
}

// Test 7: ManyNodesPersistence
TEST_F(PersistenceTest, ManyNodesPersistence) {
    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        for (int i = 0; i < 1000; i++) {
            insertfile("file_" + std::to_string(i), "/", *ptr);
        }
        EXPECT_EQ(ptr->nodeallocated, 1001);
        mmap_close_treefile(ptr, fd, mapsize);
    }

    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        EXPECT_EQ(ptr->nodeallocated, 1001);
        for (int i = 0; i < 1000; i += 100) {
            std::string name = "file_" + std::to_string(i);
            EXPECT_TRUE(hashmap_has(&ptr->hashdata, name.c_str()));
        }
        mmap_close_treefile(ptr, fd, mapsize);
    }
}

// Test 8: FreeListPersistence
// Insert, delete (create free list), save, reload, verify free list works
TEST_F(PersistenceTest, FreeListPersistence) {
    int srcAllocated = 0;

    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        for (int i = 0; i < 10; i++)
            insertfile("fl_" + std::to_string(i), "/", *ptr);
        for (int i = 0; i < 5; i++)
            delete1("fl_" + std::to_string(i), *ptr);

        // free list should have 5 entries
        EXPECT_NE(ptr->firstfree, -1);
        srcAllocated = ptr->nodeallocated;
        mmap_close_treefile(ptr, fd, mapsize);
    }

    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        // nodeallocated preserved (high-water mark)
        EXPECT_EQ(ptr->nodeallocated, srcAllocated);
        // free list preserved
        EXPECT_NE(ptr->firstfree, -1);

        // Can allocate from free list after reload
        insertfile("after_load_alloc", "/", *ptr);
        EXPECT_TRUE(hashmap_has(&ptr->hashdata, "after_load_alloc"));

        mmap_close_treefile(ptr, fd, mapsize);
    }
}

// Test 9: MetadataPersistence
TEST_F(PersistenceTest, MetadataPersistence) {
    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        insertfile("meta_file", "/", *ptr);
        int idx = hashmap_get(&ptr->hashdata, "meta_file");
        ptr->arr[idx].metadata.inode = 12345;
        ptr->arr[idx].metadata.size = 4096;
        ptr->arr[idx].metadata.mode = S_IFREG | 0755;
        ptr->arr[idx].metadata.atime = 1000000;
        ptr->arr[idx].metadata.mtime = 2000000;

        mmap_close_treefile(ptr, fd, mapsize);
    }

    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        int idx = hashmap_get(&ptr->hashdata, "meta_file");
        ASSERT_NE(idx, 0); // not root
        EXPECT_EQ(ptr->arr[idx].metadata.inode, 12345);
        EXPECT_EQ(ptr->arr[idx].metadata.size, 4096);
        EXPECT_EQ(ptr->arr[idx].metadata.mode, (mode_t)(S_IFREG | 0755));
        EXPECT_EQ(ptr->arr[idx].metadata.atime, 1000000);
        EXPECT_EQ(ptr->arr[idx].metadata.mtime, 2000000);

        mmap_close_treefile(ptr, fd, mapsize);
    }
}

// ==========================================================================
// mmap direct inspection tests
// ==========================================================================

// Test 10: MmapSiblingChainPersistence (with prevsibling)
TEST_F(PersistenceTest, MmapSiblingChainPersistence) {
    treefile* ptr = nullptr;
    int fd = -1;
    size_t mapsize = 0;
    ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

    insertfolder("parent_dir", "/", *ptr);
    for (int i = 0; i < 5; i++)
        insertfile("sib_" + std::to_string(i), "parent_dir", *ptr);

    int parentIdx = hashmap_get(&ptr->hashdata, "parent_dir");

    // Walk the sibling chain forward and verify prevsibling links
    int count = 0;
    int prev = -1;
    int cur = ptr->arr[parentIdx].firstchild;
    while (cur != -1 && cur < TREEFILE_MAX_NODES && count < 100) {
        EXPECT_FALSE(ptr->arr[cur].isdeleted);
        EXPECT_EQ(ptr->arr[cur].parent, parentIdx);
        EXPECT_EQ(ptr->arr[cur].prevsibling, prev);
        prev = cur;
        cur = ptr->arr[cur].nextsibling;
        count++;
    }
    EXPECT_EQ(count, 5);

    mmap_close_treefile(ptr, fd, mapsize);
}

// Test 11: MmapDeletedNodesOnDisk
TEST_F(PersistenceTest, MmapDeletedNodesOnDisk) {
    treefile* ptr = nullptr;
    int fd = -1;
    size_t mapsize = 0;
    ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

    insertfile("alive", "/", *ptr);
    insertfile("dead1", "/", *ptr);
    insertfile("dead2", "/", *ptr);

    int aliveIdx = hashmap_get(&ptr->hashdata, "alive");
    int dead1Idx = hashmap_get(&ptr->hashdata, "dead1");
    int dead2Idx = hashmap_get(&ptr->hashdata, "dead2");

    delete1("dead1", *ptr);
    delete1("dead2", *ptr);

    // Since writes go directly to mmap, verify on the same mapping
    EXPECT_TRUE(ptr->arr[dead1Idx].isdeleted);
    EXPECT_TRUE(ptr->arr[dead2Idx].isdeleted);
    EXPECT_EQ(ptr->arr[dead1Idx].metadata.name[0], '\0');
    EXPECT_EQ(ptr->arr[dead2Idx].metadata.name[0], '\0');

    EXPECT_FALSE(ptr->arr[aliveIdx].isdeleted);
    EXPECT_STREQ(ptr->arr[aliveIdx].metadata.name, "alive");

    mmap_close_treefile(ptr, fd, mapsize);
}

// Test 12: MmapFreeListChainOnDisk
TEST_F(PersistenceTest, MmapFreeListChainOnDisk) {
    treefile* ptr = nullptr;
    int fd = -1;
    size_t mapsize = 0;
    ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

    for (int i = 0; i < 20; i++)
        insertfile("fl_" + std::to_string(i), "/", *ptr);
    for (int i = 0; i < 10; i++)
        delete1("fl_" + std::to_string(i), *ptr);

    // Walk free list and count
    int freeCount = 0;
    int f = ptr->firstfree;
    int limit = TREEFILE_MAX_NODES;
    while (f != -1 && f < TREEFILE_MAX_NODES && limit > 0) {
        EXPECT_TRUE(ptr->arr[f].isdeleted);
        f = ptr->arr[f].nextfree;
        freeCount++;
        limit--;
    }
    EXPECT_EQ(freeCount, 10);

    mmap_close_treefile(ptr, fd, mapsize);
}

// Test 13: PrevSiblingPersistenceAfterRestart
// Verify prevsibling survives unmount/remount
TEST_F(PersistenceTest, PrevSiblingPersistenceAfterRestart) {
    int parentIdx_saved = -1;

    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        insertfolder("parent", "/", *ptr);
        insertfile("c1", "parent", *ptr);
        insertfile("c2", "parent", *ptr);
        insertfile("c3", "parent", *ptr);

        parentIdx_saved = hashmap_get(&ptr->hashdata, "parent");
        mmap_close_treefile(ptr, fd, mapsize);
    }

    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        int parentIdx = hashmap_get(&ptr->hashdata, "parent");
        EXPECT_EQ(parentIdx, parentIdx_saved);

        // Walk forward and verify prevsibling chain
        int count = 0;
        int prev = -1;
        int cur = ptr->arr[parentIdx].firstchild;
        while (cur != -1 && cur < TREEFILE_MAX_NODES && count < 100) {
            EXPECT_EQ(ptr->arr[cur].prevsibling, prev);
            prev = cur;
            cur = ptr->arr[cur].nextsibling;
            count++;
        }
        EXPECT_EQ(count, 3);

        mmap_close_treefile(ptr, fd, mapsize);
    }
}

// Test 14: HashMapPersistence
TEST_F(PersistenceTest, HashMapPersistence) {
    std::vector<std::pair<std::string, int>> records;

    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        for (int i = 0; i < 100; i++) {
            std::string name = "hash_node_" + std::to_string(i);
            insertfile(name, "/", *ptr);
            int idx = hashmap_get(&ptr->hashdata, name.c_str());
            ASSERT_NE(idx, 0);
            records.push_back({name, idx});
        }
        mmap_close_treefile(ptr, fd, mapsize);
    }

    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        for (const auto& [name, expectedIdx] : records) {
            EXPECT_TRUE(hashmap_has(&ptr->hashdata, name.c_str()))
                << "Hash missing: " << name;
            int loadedIdx = hashmap_get(&ptr->hashdata, name.c_str());
            EXPECT_EQ(loadedIdx, expectedIdx)
                << "Index mismatch for: " << name;
        }
        mmap_close_treefile(ptr, fd, mapsize);
    }
}

// Test 15: ConcurrentAccess
// Multiple threads inserting concurrently — mutex should protect
TEST_F(PersistenceTest, ConcurrentAccess) {
    treefile* ptr = nullptr;
    int fd = -1;
    size_t mapsize = 0;
    ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

    std::atomic<int> successCount(0);

    // 5 threads each inserting 10 files
    std::vector<std::thread> threads;
    for (int t = 0; t < 5; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < 10; i++) {
                std::string name = "t" + std::to_string(t) +
                                   "_f" + std::to_string(i);
                insertfile(name, "/", *ptr);
                successCount++;
            }
        });
    }

    for (auto& th : threads)
        th.join();

    EXPECT_EQ(successCount.load(), 50);

    // All 50 files should exist
    for (int t = 0; t < 5; t++) {
        for (int i = 0; i < 10; i++) {
            std::string name = "t" + std::to_string(t) +
                               "_f" + std::to_string(i);
            EXPECT_TRUE(hashmap_has(&ptr->hashdata, name.c_str()))
                << "Missing: " << name;
        }
    }

    mmap_close_treefile(ptr, fd, mapsize);
}

// Test 16: DeleteAndRecyclePersistence
// Delete nodes, close, reopen, verify free list works for recycling
TEST_F(PersistenceTest, DeleteAndRecyclePersistence) {
    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        insertfile("recycle_me", "/", *ptr);
        int deletedIdx = hashmap_get(&ptr->hashdata, "recycle_me");
        delete1("recycle_me", *ptr);
        mmap_close_treefile(ptr, fd, mapsize);
    }

    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        // Free list should have the deleted node
        EXPECT_NE(ptr->firstfree, -1);

        // New node should recycle the deleted slot
        insertfile("recycled", "/", *ptr);
        int newIdx = hashmap_get(&ptr->hashdata, "recycled");
        EXPECT_NE(newIdx, 0); // Not root
        EXPECT_FALSE(ptr->arr[newIdx].isdeleted);

        mmap_close_treefile(ptr, fd, mapsize);
    }
}

// Test 17: FileMetadataSizePersistence
// Set file size metadata, close, reopen, verify it persists
TEST_F(PersistenceTest, FileMetadataSizePersistence) {
    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        insertfile("data_file", "/", *ptr);
        int idx = hashmap_get(&ptr->hashdata, "data_file");
        ptr->arr[idx].metadata.size = 24; // Simulated file size

        mmap_close_treefile(ptr, fd, mapsize);
    }

    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        int idx = hashmap_get(&ptr->hashdata, "data_file");
        EXPECT_EQ(ptr->arr[idx].metadata.size, (off_t)24);

        mmap_close_treefile(ptr, fd, mapsize);
    }
}

// Test 18: BumpAllocatorPersistence
// Verify nodeallocated (bump pointer) persists correctly
TEST_F(PersistenceTest, BumpAllocatorPersistence) {
    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        for (int i = 0; i < 50; i++)
            insertfile("bump_" + std::to_string(i), "/", *ptr);

        EXPECT_EQ(ptr->nodeallocated, 51); // 1 root + 50 files

        // Delete some — nodeallocated should NOT change
        for (int i = 0; i < 25; i++)
            delete1("bump_" + std::to_string(i), *ptr);

        EXPECT_EQ(ptr->nodeallocated, 51); // unchanged

        mmap_close_treefile(ptr, fd, mapsize);
    }

    {
        treefile* ptr = nullptr;
        int fd = -1;
        size_t mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_file, ptr, fd, mapsize));

        EXPECT_EQ(ptr->nodeallocated, 51); // persisted as high-water mark

        mmap_close_treefile(ptr, fd, mapsize);
    }
}
