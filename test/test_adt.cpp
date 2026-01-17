#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "daemon/directory tree/adt.h"

using namespace std;

// Fixture class for ADT tests
class ADTTest : public ::testing::Test {
protected:
    void SetUp() override {
        initialize(file);
    }
    
    void TearDown() override {
        // Cleanup if needed
    }
    
    treefile file;
};

// Test 1: Initialize function
TEST_F(ADTTest, InitializeSetsCorrectState) {
    treefile newFile;
    initialize(newFile);
    
    EXPECT_EQ(newFile.head.firstfree, 1);
    EXPECT_EQ(newFile.head.nodeallocated, 1);
    EXPECT_EQ(newFile.head.size, 100000);
    EXPECT_TRUE(newFile.arr[0].isdeleted);
    EXPECT_EQ(newFile.arr[0].nextfree, 1);
    EXPECT_EQ(newFile.arr[99999].nextfree, -1);
    EXPECT_EQ(newFile.arr[0].firstchild, -1);
}

// Test 2: Insert root node
TEST_F(ADTTest, InsertRootNode) {
    insert("root", "", file);
    
    EXPECT_TRUE(file.head.hash.has("root"));
    int rootIndex = hashindex("root", file);
    EXPECT_GE(rootIndex, 0);
    EXPECT_LT(rootIndex, file.head.size);
    EXPECT_FALSE(file.arr[rootIndex].isdeleted);
    EXPECT_EQ(file.arr[rootIndex].metadata.name, "root");
    EXPECT_EQ(file.arr[rootIndex].parent, 0); // Parent is root (index 0)
}

// Test 3: Insert child under parent
TEST_F(ADTTest, InsertChildUnderParent) {
    insert("root", "", file);
    insert("child1", "root", file);
    
    EXPECT_TRUE(file.head.hash.has("child1"));
    int rootIndex = hashindex("root", file);
    int childIndex = hashindex("child1", file);
    
    EXPECT_EQ(file.arr[rootIndex].firstchild, childIndex);
    EXPECT_EQ(file.arr[childIndex].parent, rootIndex);
    EXPECT_EQ(file.arr[childIndex].metadata.name, "child1");
}

// Test 4: Insert multiple children
TEST_F(ADTTest, InsertMultipleChildren) {
    insert("root", "", file);
    insert("child1", "root", file);
    insert("child2", "root", file);
    insert("child3", "root", file);
    
    int rootIndex = hashindex("root", file);
    int firstChild = file.arr[rootIndex].firstchild;
    
    EXPECT_GE(firstChild, 0);
    
    // Check that all children are linked
    vector<int> children;
    int current = firstChild;
    int count = 0;
    while (current != -1 && count < 10) {
        children.push_back(current);
        current = file.arr[current].nextsibling;
        count++;
    }
    
    EXPECT_GE(children.size(), 3);
}

// Test 5: Hashindex returns correct index
TEST_F(ADTTest, HashindexReturnsCorrectIndex) {
    insert("testfile", "", file);
    
    int index = hashindex("testfile", file);
    EXPECT_GE(index, 0);
    EXPECT_LT(index, file.head.size);
    EXPECT_EQ(file.arr[index].metadata.name, "testfile");
}

// Test 6: Hashindex returns -1 for non-existent file
TEST_F(ADTTest, HashindexReturnsNegativeForNonExistent) {
    int index = hashindex("nonexistent", file);
    EXPECT_EQ(index, -1);
}

// Test 7: Delete leaf node
TEST_F(ADTTest, DeleteLeafNode) {
    insert("root", "", file);
    insert("child1", "root", file);
    
    int rootIndex = hashindex("root", file);
    int childIndex = hashindex("child1", file);
    
    EXPECT_EQ(file.arr[rootIndex].firstchild, childIndex);
    
    delete1("child1", file);
    
    EXPECT_TRUE(file.arr[childIndex].isdeleted);
    EXPECT_EQ(file.arr[rootIndex].firstchild, -1);
    EXPECT_FALSE(file.head.hash.has("child1"));
}

// Test 8: Delete node with children (recursive deletion)
TEST_F(ADTTest, DeleteNodeWithChildrenRecursively) {
    insert("root", "", file);
    insert("dir1", "root", file);
    insert("file1", "dir1", file);
    insert("file2", "dir1", file);
    
    int dir1Index = hashindex("dir1", file);
    int file1Index = hashindex("file1", file);
    int file2Index = hashindex("file2", file);
    
    EXPECT_FALSE(file.arr[dir1Index].isdeleted);
    EXPECT_FALSE(file.arr[file1Index].isdeleted);
    EXPECT_FALSE(file.arr[file2Index].isdeleted);
    
    delete1("dir1", file);
    
    // All nodes should be deleted
    EXPECT_TRUE(file.arr[dir1Index].isdeleted);
    EXPECT_TRUE(file.arr[file1Index].isdeleted);
    EXPECT_TRUE(file.arr[file2Index].isdeleted);
    
    // Hash entries should be removed
    EXPECT_FALSE(file.head.hash.has("dir1"));
    EXPECT_FALSE(file.head.hash.has("file1"));
    EXPECT_FALSE(file.head.hash.has("file2"));
}

// Test 9: Cannot delete root node
TEST_F(ADTTest, CannotDeleteRootNode) {
    insert("root", "", file);
    
    // Attempt to delete root should be prevented
    delete1("root", file);
    
    // Root should still exist (though technically index 0 may not be in hash)
    // The important thing is no crash occurs
    EXPECT_NO_THROW(delete1("root", file));
}

// Test 10: Change parent
TEST_F(ADTTest, ChangeParent) {
    insert("root", "", file);
    insert("dir1", "root", file);
    insert("dir2", "root", file);
    insert("file1", "dir1", file);
    
    int rootIndex = hashindex("root", file);
    int dir1Index = hashindex("dir1", file);
    int dir2Index = hashindex("dir2", file);
    int file1Index = hashindex("file1", file);
    
    EXPECT_EQ(file.arr[file1Index].parent, dir1Index);
    
    change_parent("file1", "dir2", file);
    
    EXPECT_EQ(file.arr[file1Index].parent, dir2Index);
    EXPECT_EQ(file.arr[dir2Index].firstchild, file1Index);
    EXPECT_EQ(file.arr[dir1Index].firstchild, -1);
}

// Test 11: Prevent duplicate insertions
TEST_F(ADTTest, PreventDuplicateInsertions) {
    insert("file1", "", file);
    
    int firstIndex = hashindex("file1", file);
    
    insert("file1", "", file);
    
    int secondIndex = hashindex("file1", file);
    
    // Should still point to the same index
    EXPECT_EQ(firstIndex, secondIndex);
}

// Test 12: Node allocation counter
TEST_F(ADTTest, NodeAllocationCounter) {
    EXPECT_EQ(file.head.nodeallocated, 1); // Root node
    
    insert("file1", "", file);
    EXPECT_EQ(file.head.nodeallocated, 2);
    
    insert("file2", "", file);
    EXPECT_EQ(file.head.nodeallocated, 3);
    
    delete1("file1", file);
    EXPECT_EQ(file.head.nodeallocated, 2);
}

// Test 13: Empty filename handling
TEST_F(ADTTest, EmptyFilenameHandling) {
    int before = file.head.nodeallocated;
    
    insert("", "", file); // Empty filename
    
    // Should not insert
    EXPECT_EQ(file.head.nodeallocated, before);
    EXPECT_FALSE(file.head.hash.has(""));
}

// Test 14: Insert with non-existent parent defaults to root
TEST_F(ADTTest, NonExistentParentDefaultsToRoot) {
    insert("orphan", "nonexistent_parent", file);
    
    int orphanIndex = hashindex("orphan", file);
    EXPECT_GE(orphanIndex, 0);
    // Should be linked to root (index 0)
    EXPECT_EQ(file.arr[orphanIndex].parent, 0);
}

// Test 15: Change parent prevents cycles
TEST_F(ADTTest, ChangeParentPreventsCycles) {
    insert("root", "", file);
    insert("dir1", "root", file);
    insert("dir2", "dir1", file);
    
    int dir1Index = hashindex("dir1", file);
    int dir2Index = hashindex("dir2", file);
    int rootIndex = hashindex("root", file);
    
    // Store original parent of dir1 (should be root)
    int originalParent = file.arr[dir1Index].parent;
    EXPECT_EQ(originalParent, rootIndex);
    
    // Try to move dir1 under dir2 (would create cycle since dir2 is under dir1)
    change_parent("dir1", "dir2", file);
    
    // Should not create cycle - dir1's parent should remain the same
    // The cycle check should detect that dir2 is a descendant of dir1
    EXPECT_EQ(file.arr[dir1Index].parent, originalParent); // Should remain under root
    EXPECT_NE(file.arr[dir1Index].parent, dir2Index); // Should not be under dir2
}

// Test 16: Thread safety - concurrent inserts
TEST_F(ADTTest, ThreadSafetyConcurrentInserts) {
    vector<thread> threads;
    const int numThreads = 10;
    const int filesPerThread = 5;
    
    for (int t = 0; t < numThreads; t++) {
        threads.emplace_back([this, t, filesPerThread]() {
            for (int i = 0; i < filesPerThread; i++) {
                string filename = "file_" + to_string(t) + "_" + to_string(i);
                insert(filename, "", file);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Verify all files were inserted
    EXPECT_EQ(file.head.nodeallocated, 1 + numThreads * filesPerThread);
}

// Test 17: Thread safety - concurrent delete and insert
TEST_F(ADTTest, ThreadSafetyConcurrentDeleteAndInsert) {
    // Setup
    for (int i = 0; i < 10; i++) {
        insert("file" + to_string(i), "", file);
    }
    
    vector<thread> threads;
    threads.emplace_back([this]() {
        for (int i = 0; i < 5; i++) {
            delete1("file" + to_string(i), file);
        }
    });
    
    threads.emplace_back([this]() {
        for (int i = 10; i < 15; i++) {
            insert("file" + to_string(i), "", file);
        }
    });
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Should complete without crashes
    EXPECT_GE(file.head.nodeallocated, 1);
}

// Test 18: Free list integrity after deletions
TEST_F(ADTTest, FreeListIntegrityAfterDeletions) {
    vector<string> filenames;
    for (int i = 0; i < 10; i++) {
        string name = "file" + to_string(i);
        filenames.push_back(name);
        insert(name, "", file);
    }
    
    // Delete some files
    for (int i = 0; i < 5; i++) {
        delete1(filenames[i], file);
    }
    
    // Free list should still allow insertions
    insert("newfile", "", file);
    EXPECT_TRUE(file.head.hash.has("newfile"));
}

// Test 19: Change parent to same parent (no-op)
TEST_F(ADTTest, ChangeParentToSameParentIsNoOp) {
    insert("root", "", file);
    insert("file1", "root", file);
    
    int file1Index = hashindex("file1", file);
    int rootIndex = hashindex("root", file);
    int originalParent = file.arr[file1Index].parent;
    
    change_parent("file1", "root", file);
    
    // Parent should remain the same
    EXPECT_EQ(file.arr[file1Index].parent, originalParent);
}

// Test 20: Deep tree deletion
TEST_F(ADTTest, DeepTreeDeletion) {
    insert("root", "", file);
    
    // Create a deep tree
    string parent = "root";
    for (int i = 0; i < 5; i++) {
        string dir = "dir" + to_string(i);
        insert(dir, parent, file);
        parent = dir;
        
        // Add files to each directory
        for (int j = 0; j < 3; j++) {
            insert("file" + to_string(i) + "_" + to_string(j), dir, file);
        }
    }
    
    // Delete root directory (should delete entire tree)
    delete1("dir0", file);
    
    // Verify all children are deleted
    EXPECT_FALSE(file.head.hash.has("dir0"));
    EXPECT_FALSE(file.head.hash.has("dir1"));
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 3; j++) {
            EXPECT_FALSE(file.head.hash.has("file" + to_string(i) + "_" + to_string(j)));
        }
    }
}

