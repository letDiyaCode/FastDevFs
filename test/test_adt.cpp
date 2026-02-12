#include <gtest/gtest.h>
#include "../include/daemon/directory tree/adt.h"

class AdtTest : public ::testing::Test {
protected:
    treefile* file;

    void SetUp() override {
        file = new treefile();
        initialize(*file);
    }

    void TearDown() override {
        delete file;
    }
};

TEST_F(AdtTest, Initialize) {
    EXPECT_EQ(file->head.nodeallocated, 1); // 0 reserved for root
    EXPECT_EQ(file->head.firstfree, 1);
    EXPECT_EQ(file->head.size, 100000);
}

TEST_F(AdtTest, InsertFolder) {
    insertfolder("folder1", "", *file);
    int index = hashindex("folder1", *file);
    ASSERT_NE(index, -1);
    EXPECT_EQ(std::string(file->arr[index].metadata.name), "folder1");
    EXPECT_FALSE(file->arr[index].isdeleted);
    EXPECT_EQ(file->arr[index].metadata.mode & S_IFMT, S_IFDIR);
}

TEST_F(AdtTest, InsertFile) {
    insertfile("file1", "", *file);
    int index = hashindex("file1", *file);
    ASSERT_NE(index, -1);
    EXPECT_EQ(std::string(file->arr[index].metadata.name), "file1");
    EXPECT_FALSE(file->arr[index].isdeleted);
    EXPECT_EQ(file->arr[index].metadata.mode & S_IFMT, S_IFREG);
}

TEST_F(AdtTest, DeleteFile) {
    insertfile("file2", "", *file);
    int index = hashindex("file2", *file);
    ASSERT_NE(index, -1);
    
    delete1("file2", *file);
    index = hashindex("file2", *file);
    EXPECT_EQ(index, -1);
}

TEST_F(AdtTest, ParentChildRelationship) {
    insertfolder("parent", "", *file);
    insertfile("child", "parent", *file);
    
    int parentIdx = hashindex("parent", *file);
    int childIdx = hashindex("child", *file);
    
    ASSERT_NE(parentIdx, -1);
    ASSERT_NE(childIdx, -1);
    
    EXPECT_EQ(file->arr[childIdx].parent, parentIdx);
    EXPECT_EQ(file->arr[parentIdx].firstchild, childIdx);
}

TEST_F(AdtTest, ChangeParent) {
    insertfolder("dir1", "", *file);
    insertfolder("dir2", "", *file);
    insertfile("file3", "dir1", *file);
    
    int dir1Idx = hashindex("dir1", *file);
    int dir2Idx = hashindex("dir2", *file);
    int fileIdx = hashindex("file3", *file);
    
    ASSERT_EQ(file->arr[fileIdx].parent, dir1Idx);
    
    change_parent("file3", "dir2", *file);
    
    EXPECT_EQ(file->arr[fileIdx].parent, dir2Idx);
    // Verify file is in dir2's children list (simplified check: it is the first child or reachable)
    EXPECT_EQ(file->arr[dir2Idx].firstchild, fileIdx); 
}

TEST_F(AdtTest, PreventCycle) {
    insertfolder("A", "", *file);
    insertfolder("B", "A", *file);
    
    // Try to move A under B (should fail)
    change_parent("A", "B", *file);
    
    int aIdx = hashindex("A", *file);
    // Parent of A should still be root (0) or -1 if we consider root logic
    // In insertfolder("", "", *file) is not possible, root is implicitly 0.
    // Logic: `insertfolder("A", "", ...)` sets A's parent to 0 (root).
    EXPECT_EQ(file->arr[aIdx].parent, 0);
}

TEST_F(AdtTest, SearchFile) {
    insertfile("searchFile", "", *file);
    int index = hashindex("searchFile", *file);
    ASSERT_NE(index, -1);
    EXPECT_EQ(std::string(file->arr[index].metadata.name), "searchFile");
}

TEST_F(AdtTest, SearchFolder) {
    insertfolder("searchFolder", "", *file);
    int index = hashindex("searchFolder", *file);
    ASSERT_NE(index, -1);
    EXPECT_EQ(std::string(file->arr[index].metadata.name), "searchFolder");
}

TEST_F(AdtTest, SearchNonExistent) {
    int index = hashindex("nonExistent", *file);
    EXPECT_EQ(index, -1);
}

TEST_F(AdtTest, RootNodeProperties) {
    // Root is always at index 0 after initialize
    EXPECT_FALSE(file->arr[0].isdeleted);
    // Name might be empty or "/" depending on implementation details not fully visible but let's check basic validity
    // Actually, initialize() sets `nodeallocated = 1`, implicitly considering 0 used.
    // Let's verify it's not marked free.
    EXPECT_NE(file->head.firstfree, 0); 
}

TEST_F(AdtTest, DeleteFolder) {
    insertfolder("deleteMe", "", *file);
    int index = hashindex("deleteMe", *file);
    ASSERT_NE(index, -1);
    
    delete1("deleteMe", *file);
    index = hashindex("deleteMe", *file);
    EXPECT_EQ(index, -1);
}

TEST_F(AdtTest, DeleteNonExistent) {
    // Should not crash
    delete1("ghostFile", *file);
    EXPECT_EQ(hashindex("ghostFile", *file), -1);
}

TEST_F(AdtTest, InsertDeepHierarchy) {
    insertfolder("level1", "", *file);
    insertfolder("level2", "level1", *file);
    insertfile("deepFile", "level2", *file);
    
    int fIdx = hashindex("deepFile", *file);
    int l2Idx = hashindex("level2", *file);
    
    ASSERT_NE(fIdx, -1);
    EXPECT_EQ(file->arr[fIdx].parent, l2Idx);
}

TEST_F(AdtTest, ChangeParentNonExistent) {
    // Try to move a non-existent file
    // Should not crash
    change_parent("ghostMover", "root", *file);
    EXPECT_EQ(hashindex("ghostMover", *file), -1);
}

TEST_F(AdtTest, ChangeParentToNonExistent) {
    insertfile("mover", "", *file);
    // Move to non-existent parent
    change_parent("mover", "ghostParent", *file);
    
    // Parent should remain as it was (likely root, 0)
    int idx = hashindex("mover", *file);
    // If implementation is robust, it shouldn't change or should handle error.
    // Assuming 0 is root/default parent
    EXPECT_EQ(file->arr[idx].parent, 0); 
}

TEST_F(AdtTest, ReInitialize) {
    insertfile("wipedFile", "", *file);
    EXPECT_NE(hashindex("wipedFile", *file), -1);
    
    initialize(*file);
    
    EXPECT_EQ(hashindex("wipedFile", *file), -1);
    EXPECT_EQ(file->head.nodeallocated, 1);
}
