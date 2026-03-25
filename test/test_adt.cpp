#include <gtest/gtest.h>
#include <unistd.h>
#include "../include/daemon/directory tree/adt.h"

class AdtTest : public ::testing::Test {
protected:
    treefile* file;
    int fd;
    size_t mapsize;
    const char* test_path = "/tmp/test_adt.mmap";

    void SetUp() override {
        unlink(test_path);
        file = nullptr;
        fd = -1;
        mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_path, file, fd, mapsize));
    }

    void TearDown() override {
        mmap_close_treefile(file, fd, mapsize);
        unlink(test_path);
    }
};

TEST_F(AdtTest, Initialize) {
    EXPECT_EQ(file->nodeallocated, 1); // 0 reserved for root
    EXPECT_EQ(file->firstfree, -1);    // No pre-built free list (bump allocator)
    EXPECT_EQ(file->size, TREEFILE_MAX_NODES);
}

TEST_F(AdtTest, InsertFolder) {
    insertfolder("/folder1", "/", *file);
    int index = hashindex("/folder1", *file);
    ASSERT_NE(index, -1);
    EXPECT_STREQ(file->arr[index].metadata.name, "/folder1");
    EXPECT_FALSE(file->arr[index].isdeleted);
    EXPECT_EQ(file->arr[index].metadata.mode & S_IFMT, S_IFDIR);
}

TEST_F(AdtTest, InsertFile) {
    insertfile("/file1", "/", *file);
    int index = hashindex("/file1", *file);
    ASSERT_NE(index, -1);
    EXPECT_STREQ(file->arr[index].metadata.name, "/file1");
    EXPECT_FALSE(file->arr[index].isdeleted);
    EXPECT_EQ(file->arr[index].metadata.mode & S_IFMT, S_IFREG);
}

TEST_F(AdtTest, DeleteFile) {
    insertfile("/file2", "/", *file);
    int index = hashindex("/file2", *file);
    ASSERT_NE(index, -1);

    delete1("/file2", *file);
    index = hashindex("/file2", *file);
    EXPECT_EQ(index, -1);
}

TEST_F(AdtTest, ParentChildRelationship) {
    insertfolder("/parent", "/", *file);
    insertfile("/parent/child", "/parent", *file);

    int parentIdx = hashindex("/parent", *file);
    int childIdx = hashindex("/parent/child", *file);

    ASSERT_NE(parentIdx, -1);
    ASSERT_NE(childIdx, -1);

    EXPECT_EQ(file->arr[childIdx].parent, parentIdx);
    EXPECT_EQ(file->arr[parentIdx].firstchild, childIdx);
}

TEST_F(AdtTest, ChangeParent) {
    insertfolder("/dir1", "/", *file);
    insertfolder("/dir2", "/", *file);
    insertfile("/dir1/file3", "/dir1", *file);

    int dir1Idx = hashindex("/dir1", *file);
    int dir2Idx = hashindex("/dir2", *file);
    int fileIdx = hashindex("/dir1/file3", *file);

    ASSERT_EQ(file->arr[fileIdx].parent, dir1Idx);

    change_parent("/dir1/file3", "/dir2", *file);

    // After move, file3 should be at /dir2/file3
    int newFileIdx = hashindex("/dir2/file3", *file);
    ASSERT_NE(newFileIdx, -1);
    EXPECT_EQ(newFileIdx, fileIdx); // Same slot, just new path
    EXPECT_EQ(file->arr[newFileIdx].parent, dir2Idx);
    EXPECT_EQ(file->arr[dir2Idx].firstchild, newFileIdx);

    // Old path should no longer exist
    EXPECT_EQ(hashindex("/dir1/file3", *file), -1);
}

TEST_F(AdtTest, PreventCycle) {
    insertfolder("/A", "/", *file);
    insertfolder("/A/B", "/A", *file);

    // Try to move A under B (should fail — would create cycle)
    change_parent("/A", "/A/B", *file);

    int aIdx = hashindex("/A", *file);
    EXPECT_EQ(file->arr[aIdx].parent, 0); // A should still be under root
}

TEST_F(AdtTest, SearchFile) {
    insertfile("/searchFile", "/", *file);
    int index = hashindex("/searchFile", *file);
    ASSERT_NE(index, -1);
    EXPECT_STREQ(file->arr[index].metadata.name, "/searchFile");
}

TEST_F(AdtTest, SearchNonExistent) {
    int index = hashindex("/nonExistent", *file);
    EXPECT_EQ(index, -1);
}

TEST_F(AdtTest, RootNodeProperties) {
    EXPECT_FALSE(file->arr[0].isdeleted);
    EXPECT_STREQ(file->arr[0].metadata.name, "/");
    EXPECT_TRUE(S_ISDIR(file->arr[0].metadata.mode));
    EXPECT_NE(file->firstfree, 0); // Root is never on free list
}

TEST_F(AdtTest, DeleteFolder) {
    insertfolder("/deleteMe", "/", *file);
    int index = hashindex("/deleteMe", *file);
    ASSERT_NE(index, -1);

    delete1("/deleteMe", *file);
    index = hashindex("/deleteMe", *file);
    EXPECT_EQ(index, -1);
}

TEST_F(AdtTest, DeleteNonExistent) {
    delete1("/ghostFile", *file);
    EXPECT_EQ(hashindex("/ghostFile", *file), -1);
}

TEST_F(AdtTest, InsertDeepHierarchy) {
    insertfolder("/level1", "/", *file);
    insertfolder("/level1/level2", "/level1", *file);
    insertfile("/level1/level2/deepFile", "/level1/level2", *file);

    int fIdx = hashindex("/level1/level2/deepFile", *file);
    int l2Idx = hashindex("/level1/level2", *file);

    ASSERT_NE(fIdx, -1);
    EXPECT_EQ(file->arr[fIdx].parent, l2Idx);
}

TEST_F(AdtTest, ChangeParentNonExistent) {
    change_parent("/ghostMover", "/", *file);
    EXPECT_EQ(hashindex("/ghostMover", *file), -1);
}

TEST_F(AdtTest, ChangeParentToNonExistent) {
    insertfile("/mover", "/", *file);
    change_parent("/mover", "/ghostParent", *file);

    int idx = hashindex("/mover", *file);
    EXPECT_EQ(file->arr[idx].parent, 0); // Should remain under root
}

TEST_F(AdtTest, ReInitialize) {
    insertfile("/wipedFile", "/", *file);
    EXPECT_NE(hashindex("/wipedFile", *file), -1);

    initialize(*file);

    EXPECT_EQ(hashindex("/wipedFile", *file), -1);
    EXPECT_EQ(file->nodeallocated, 1);
}

// ============================================================
// New tests for doubly linked sibling list and bump allocator
// ============================================================

TEST_F(AdtTest, SiblingDoublyLinked) {
    insertfolder("/parent", "/", *file);
    insertfile("/parent/child1", "/parent", *file);
    insertfile("/parent/child2", "/parent", *file);
    insertfile("/parent/child3", "/parent", *file);

    int parentIdx = hashindex("/parent", *file);
    int c1 = hashindex("/parent/child1", *file);
    int c2 = hashindex("/parent/child2", *file);
    int c3 = hashindex("/parent/child3", *file);
    ASSERT_NE(parentIdx, -1);
    ASSERT_NE(c1, -1);
    ASSERT_NE(c2, -1);
    ASSERT_NE(c3, -1);

    // Prepend order: child3 -> child2 -> child1 -> -1
    EXPECT_EQ(file->arr[parentIdx].firstchild, c3);
    EXPECT_EQ(file->arr[c3].nextsibling, c2);
    EXPECT_EQ(file->arr[c2].nextsibling, c1);
    EXPECT_EQ(file->arr[c1].nextsibling, -1);

    // Verify prevsibling chain: child1 <- child2 <- child3(head, prev=-1)
    EXPECT_EQ(file->arr[c3].prevsibling, -1);
    EXPECT_EQ(file->arr[c2].prevsibling, c3);
    EXPECT_EQ(file->arr[c1].prevsibling, c2);
}

TEST_F(AdtTest, DeleteMiddleChildO1Unlink) {
    insertfolder("/parent", "/", *file);
    insertfile("/parent/child1", "/parent", *file);
    insertfile("/parent/child2", "/parent", *file);
    insertfile("/parent/child3", "/parent", *file);

    int parentIdx = hashindex("/parent", *file);
    int c1 = hashindex("/parent/child1", *file);
    int c3 = hashindex("/parent/child3", *file);

    // Delete middle child (child2) — should be O(1) via prevsibling
    delete1("/parent/child2", *file);
    EXPECT_EQ(hashindex("/parent/child2", *file), -1);

    // Verify sibling chain: child3 -> child1 -> -1
    EXPECT_EQ(file->arr[parentIdx].firstchild, c3);
    EXPECT_EQ(file->arr[c3].nextsibling, c1);
    EXPECT_EQ(file->arr[c1].nextsibling, -1);

    // Verify prevsibling: child1 <- child3(head)
    EXPECT_EQ(file->arr[c3].prevsibling, -1);
    EXPECT_EQ(file->arr[c1].prevsibling, c3);
}

TEST_F(AdtTest, DeleteFirstChild) {
    insertfolder("/parent", "/", *file);
    insertfile("/parent/child1", "/parent", *file);
    insertfile("/parent/child2", "/parent", *file);

    int parentIdx = hashindex("/parent", *file);
    int c1 = hashindex("/parent/child1", *file);

    // Delete first child (child2, which was prepended last)
    delete1("/parent/child2", *file);

    // child1 should now be the first child
    EXPECT_EQ(file->arr[parentIdx].firstchild, c1);
    EXPECT_EQ(file->arr[c1].prevsibling, -1);
    EXPECT_EQ(file->arr[c1].nextsibling, -1);
}

TEST_F(AdtTest, BumpAllocator) {
    // With bump allocator, nodeallocated only increases
    EXPECT_EQ(file->nodeallocated, 1); // root

    insertfile("/f1", "/", *file);
    EXPECT_EQ(file->nodeallocated, 2);

    insertfile("/f2", "/", *file);
    EXPECT_EQ(file->nodeallocated, 3);

    // Delete f1 — nodeallocated should NOT decrease
    delete1("/f1", *file);
    EXPECT_EQ(file->nodeallocated, 3); // high-water mark unchanged

    // f1's slot should be on the free list
    EXPECT_NE(file->firstfree, -1);
}

TEST_F(AdtTest, FreeListRecycling) {
    insertfile("/f1", "/", *file);
    insertfile("/f2", "/", *file);

    int f1Idx = hashindex("/f1", *file);
    delete1("/f1", *file);

    // Allocate a new file — should reuse f1's slot (free list priority)
    insertfile("/f3", "/", *file);
    int f3Idx = hashindex("/f3", *file);
    EXPECT_EQ(f3Idx, f1Idx); // Recycled slot
}

TEST_F(AdtTest, ChangeParentPrevSiblingIntegrity) {
    insertfolder("/dirA", "/", *file);
    insertfolder("/dirB", "/", *file);
    insertfile("/dirA/f1", "/dirA", *file);
    insertfile("/dirA/f2", "/dirA", *file);
    insertfile("/dirA/f3", "/dirA", *file);

    int dirBIdx = hashindex("/dirB", *file);
    int f1Idx = hashindex("/dirA/f1", *file);
    int f3Idx = hashindex("/dirA/f3", *file);
    int dirAIdx = hashindex("/dirA", *file);

    // Move f2 (middle child) to dirB
    change_parent("/dirA/f2", "/dirB", *file);

    // After move, f2 is now at /dirB/f2
    int f2Idx = hashindex("/dirB/f2", *file);
    ASSERT_NE(f2Idx, -1);

    // Verify dirA's children: f3 -> f1 (f2 removed)
    EXPECT_EQ(file->arr[dirAIdx].firstchild, f3Idx);
    EXPECT_EQ(file->arr[f3Idx].nextsibling, f1Idx);
    EXPECT_EQ(file->arr[f1Idx].prevsibling, f3Idx);
    EXPECT_EQ(file->arr[f1Idx].nextsibling, -1);

    // Verify dirB's children: f2 only
    EXPECT_EQ(file->arr[dirBIdx].firstchild, f2Idx);
    EXPECT_EQ(file->arr[f2Idx].prevsibling, -1);
    EXPECT_EQ(file->arr[f2Idx].nextsibling, -1);
    EXPECT_EQ(file->arr[f2Idx].parent, dirBIdx);
}

// ============================================================
// New test: same-named files in different directories
// ============================================================

TEST_F(AdtTest, SameNameDifferentDirs) {
    insertfolder("/dir1", "/", *file);
    insertfolder("/dir2", "/", *file);
    insertfile("/dir1/file.txt", "/dir1", *file);
    insertfile("/dir2/file.txt", "/dir2", *file);

    // Both should exist as distinct entries
    int idx1 = hashindex("/dir1/file.txt", *file);
    int idx2 = hashindex("/dir2/file.txt", *file);
    EXPECT_NE(idx1, -1);
    EXPECT_NE(idx2, -1);
    EXPECT_NE(idx1, idx2); // Different slots

    // Verify correct parents
    int dir1Idx = hashindex("/dir1", *file);
    int dir2Idx = hashindex("/dir2", *file);
    EXPECT_EQ(file->arr[idx1].parent, dir1Idx);
    EXPECT_EQ(file->arr[idx2].parent, dir2Idx);
}

TEST_F(AdtTest, DeleteSubtreeUpdatesHash) {
    insertfolder("/top", "/", *file);
    insertfolder("/top/mid", "/top", *file);
    insertfile("/top/mid/leaf", "/top/mid", *file);

    // Delete top — should recursively delete mid and leaf
    delete1("/top", *file);

    EXPECT_EQ(hashindex("/top", *file), -1);
    EXPECT_EQ(hashindex("/top/mid", *file), -1);
    EXPECT_EQ(hashindex("/top/mid/leaf", *file), -1);
}

TEST_F(AdtTest, ChangeParentUpdatesDescendantPaths) {
    insertfolder("/src", "/", *file);
    insertfolder("/src/sub", "/src", *file);
    insertfile("/src/sub/file.txt", "/src/sub", *file);
    insertfolder("/dst", "/", *file);

    // Move /src to /dst
    change_parent("/src", "/dst", *file);

    // Old paths gone
    EXPECT_EQ(hashindex("/src", *file), -1);
    EXPECT_EQ(hashindex("/src/sub", *file), -1);
    EXPECT_EQ(hashindex("/src/sub/file.txt", *file), -1);

    // New paths exist
    EXPECT_NE(hashindex("/dst/src", *file), -1);
    EXPECT_NE(hashindex("/dst/src/sub", *file), -1);
    EXPECT_NE(hashindex("/dst/src/sub/file.txt", *file), -1);

    // Verify parent chain
    int srcIdx = hashindex("/dst/src", *file);
    int subIdx = hashindex("/dst/src/sub", *file);
    int fileIdx = hashindex("/dst/src/sub/file.txt", *file);
    int dstIdx = hashindex("/dst", *file);

    EXPECT_EQ(file->arr[srcIdx].parent, dstIdx);
    EXPECT_EQ(file->arr[subIdx].parent, srcIdx);
    EXPECT_EQ(file->arr[fileIdx].parent, subIdx);
}
