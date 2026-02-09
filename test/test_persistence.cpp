#include <gtest/gtest.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstring>
#include <thread>
#include <vector>
#include "daemon/directory tree/adt.h"

using namespace std;

// Test fixture for persistence tests
class PersistenceTest : public ::testing::Test {
protected:
    const char* test_file = "/tmp/test_treefile.mmap";
    
    void SetUp() override {
        // Remove test file if it exists
        unlink(test_file);
    }
    
    void TearDown() override {
        // Cleanup test file
        unlink(test_file);
    }
};

// Test 1: Basic save and load
TEST_F(PersistenceTest, BasicSaveLoad) {
    treefile* file1 = new treefile();
    initialize(*file1);
    
    // Insert some data
    insert("root", "", *file1);
    insert("dir1", "root", *file1);
    insert("file1", "dir1", *file1);
    
    // Save to disk
    EXPECT_TRUE(save_treefile(test_file, *file1));
    
    // Load into new treefile
    treefile* file2 = new treefile();
    EXPECT_TRUE(load_treefile(test_file, *file2));
    
    // Verify data matches
    EXPECT_TRUE(file2->head.hash.has("root"));
    EXPECT_TRUE(file2->head.hash.has("dir1"));
    EXPECT_TRUE(file2->head.hash.has("file1"));
    
    int rootIdx = hashindex("root", *file2);
    int dir1Idx = hashindex("dir1", *file2);
    int file1Idx = hashindex("file1", *file2);
    
    EXPECT_GE(rootIdx, 0);
    EXPECT_GE(dir1Idx, 0);
    EXPECT_GE(file1Idx, 0);
    
    EXPECT_EQ(file2->arr[file1Idx].parent, dir1Idx);
    EXPECT_EQ(file2->arr[dir1Idx].parent, rootIdx);
}

// Test 2: Persistence across "restarts"
TEST_F(PersistenceTest, PersistenceAcrossRestarts) {
    {
        treefile* file1 = new treefile();
        initialize(*file1);
        
        insert("root", "", *file1);
        insert("documents", "root", *file1);
        insert("pictures", "root", *file1);
        insert("file.txt", "documents", *file1);
        
        EXPECT_TRUE(save_treefile(test_file, *file1));
        // file1 goes out of scope (simulating process restart)
    }
    
    {
        treefile* file2 = new treefile();
        EXPECT_TRUE(load_treefile(test_file, *file2));
        
        EXPECT_TRUE(file2->head.hash.has("root"));
        EXPECT_TRUE(file2->head.hash.has("documents"));
        EXPECT_TRUE(file2->head.hash.has("pictures"));
        EXPECT_TRUE(file2->head.hash.has("file.txt"));
        
        EXPECT_EQ(file2->head.nodeallocated, 5); // root pre-allocated + 4 inserted
    }
}

// Test 3: Save after modifications
TEST_F(PersistenceTest, SaveAfterModifications) {
    treefile* file1 = new treefile();
    initialize(*file1);
    
    insert("root", "", *file1);
    insert("temp1", "root", *file1);
    insert("temp2", "root", *file1);
    insert("keep", "root", *file1);
    
    // Save state with 4 nodes
    EXPECT_TRUE(save_treefile(test_file, *file1));
    
    // Delete some nodes
    delete1("temp1", *file1);
    delete1("temp2", *file1);
    
    // Load saved state (should restore deleted nodes)
    treefile* file2 = new treefile();
    EXPECT_TRUE(load_treefile(test_file, *file2));
    
    EXPECT_TRUE(file2->head.hash.has("temp1"));
    EXPECT_TRUE(file2->head.hash.has("temp2"));
    EXPECT_TRUE(file2->head.hash.has("keep"));
}

// Test 4: Load non-existent file
TEST_F(PersistenceTest, LoadNonExistentFile) {
    treefile* file1 = new treefile();
    
    EXPECT_FALSE(load_treefile("/tmp/nonexistent_file_12345.mmap", *file1));
}

// Test 5: Load corrupted file (wrong size)
TEST_F(PersistenceTest, LoadCorruptedFile) {
    // Create a file with wrong size
    int fd = open(test_file, O_RDWR | O_CREAT | O_TRUNC, 0644);
    ASSERT_NE(fd, -1);
    
    const char* dummy_data = "This is not a valid treefile";
    write(fd, dummy_data, strlen(dummy_data));
    close(fd);
    
    treefile* file1 = new treefile();
    EXPECT_FALSE(load_treefile(test_file, *file1));
}

// Test 6: init_or_load creates new file
TEST_F(PersistenceTest, InitOrLoadCreatesNew) {
    treefile* file1 = new treefile();
    
    EXPECT_TRUE(init_or_load_treefile(test_file, *file1));
    
    // Should have initialized
    EXPECT_EQ(file1->head.firstfree, 1);
    EXPECT_EQ(file1->head.nodeallocated, 1);
    
    // File should exist now
    struct stat st;
    EXPECT_EQ(stat(test_file, &st), 0);
}

// Test 7: init_or_load loads existing
TEST_F(PersistenceTest, InitOrLoadLoadsExisting) {
    // Create and save a tree
    {
        treefile* file1 = new treefile();
        initialize(*file1);
        insert("root", "", *file1);
        insert("existing", "root", *file1);
        EXPECT_TRUE(save_treefile(test_file, *file1));
    }
    
    // Load using init_or_load
    treefile* file2 = new treefile();
    EXPECT_TRUE(init_or_load_treefile(test_file, *file2));
    
    EXPECT_TRUE(file2->head.hash.has("existing"));
}

// Test 8: Persistence with deep tree
TEST_F(PersistenceTest, PersistenceWithDeepTree) {
    treefile* file1 = new treefile();
    initialize(*file1);
    
    // Create deep tree
    insert("root", "", *file1);
    string parent = "root";
    for (int i = 0; i < 10; i++) {
        string dir = "dir" + to_string(i);
        insert(dir, parent, *file1);
        parent = dir;
    }
    
    EXPECT_TRUE(save_treefile(test_file, *file1));
    
    treefile* file2 = new treefile();
    EXPECT_TRUE(load_treefile(test_file, *file2));
    
    // Verify all nodes exist
    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(file2->head.hash.has("dir" + to_string(i)));
    }
}

// Test 9: Persistence with many nodes
TEST_F(PersistenceTest, PersistenceWithManyNodes) {
    treefile* file1 = new treefile();
    initialize(*file1);
    
    insert("root", "", *file1);
    
    // Insert 1000 nodes
    for (int i = 0; i < 1000; i++) {
        insert("file" + to_string(i), "root", *file1);
    }
    
    EXPECT_TRUE(save_treefile(test_file, *file1));
    
    treefile* file2 = new treefile();
    EXPECT_TRUE(load_treefile(test_file, *file2));
    
    // Verify all nodes exist
    EXPECT_EQ(file2->head.nodeallocated, 1002); // root pre-allocated + root + 1000 files
    for (int i = 0; i < 1000; i++) {
        EXPECT_TRUE(file2->head.hash.has("file" + to_string(i)));
    }
}

// Test 10: Concurrent save operations (stress test)
TEST_F(PersistenceTest, ConcurrentSaveLoad) {
    treefile* file1 = new treefile();
    initialize(*file1);
    
    insert("root", "", *file1);
    for (int i = 0; i < 50; i++) {
        insert("file" + to_string(i), "root", *file1);
    }
    
    vector<thread> threads;
    bool success = true;
    
    // Multiple threads saving concurrently (mutex should protect)
    for (int t = 0; t < 5; t++) {
        threads.emplace_back([&file1, this, &success]() {
            for (int i = 0; i < 10; i++) {
                if (!save_treefile(test_file, *file1)) {
                    success = false;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_TRUE(success);
    
    // Verify we can load the file
    treefile* file2 = new treefile();
    EXPECT_TRUE(load_treefile(test_file, *file2));
}

// Test 11: HashMap persistence
TEST_F(PersistenceTest, HashMapPersistence) {
    treefile* file1 = new treefile();
    initialize(*file1);
    
    vector<string> filenames;
    for (int i = 0; i < 100; i++) {
        string name = "file_" + to_string(i);
        filenames.push_back(name);
        insert(name, "", *file1);
    }
    
    EXPECT_TRUE(save_treefile(test_file, *file1));
    
    treefile* file2 = new treefile();
    EXPECT_TRUE(load_treefile(test_file, *file2));
    
    // Verify all hash entries exist and point to correct indices
    for (const auto& name : filenames) {
        EXPECT_TRUE(file2->head.hash.has(name));
        int idx1 = hashindex(name, *file1);
        int idx2 = hashindex(name, *file2);
        EXPECT_EQ(idx1, idx2);
    }
}

// Test 12: Free list persistence
TEST_F(PersistenceTest, FreeListPersistence) {
    treefile* file1 = new treefile();
    initialize(*file1);
    
    // Insert and delete to create free list
    insert("root", "", *file1);
    for (int i = 0; i < 10; i++) {
        insert("temp" + to_string(i), "root", *file1);
    }
    
    // Delete half of them
    for (int i = 0; i < 5; i++) {
        delete1("temp" + to_string(i), *file1);
    }
    
    int free_count_before = 0;
    int free_idx = file1->head.firstfree;
    while (free_idx != -1 && free_count_before < 10000) {
        free_count_before++;
        free_idx = file1->arr[free_idx].nextfree;
    }
    
    EXPECT_TRUE(save_treefile(test_file, *file1));
    
    treefile* file2 = new treefile();
    EXPECT_TRUE(load_treefile(test_file, *file2));
    
    // Verify free list is intact
    int free_count_after = 0;
    free_idx = file2->head.firstfree;
    while (free_idx != -1 && free_count_after < 10000) {
        free_count_after++;
        free_idx = file2->arr[free_idx].nextfree;
    }
    
    EXPECT_EQ(free_count_before, free_count_after);
    
    // Should be able to insert new nodes (using free list)
    insert("newfile", "root", *file2);
    EXPECT_TRUE(file2->head.hash.has("newfile"));
}

// Test 13: Metadata persistence
TEST_F(PersistenceTest, MetadataPersistence) {
    treefile* file1 = new treefile();
    initialize(*file1);
    
    insert("root", "", *file1);
    insert("file1", "root", *file1);
    insert("file2", "root", *file1);
    
    // Set inode values
    int idx1 = hashindex("file1", *file1);
    int idx2 = hashindex("file2", *file1);
    file1->arr[idx1].metadata.inode = 12345;
    file1->arr[idx2].metadata.inode = 67890;
    
    EXPECT_TRUE(save_treefile(test_file, *file1));
    
    treefile* file2 = new treefile();
    EXPECT_TRUE(load_treefile(test_file, *file2));
    
    int idx1_loaded = hashindex("file1", *file2);
    int idx2_loaded = hashindex("file2", *file2);
    
    EXPECT_EQ(file2->arr[idx1_loaded].metadata.inode, 12345);
    EXPECT_EQ(file2->arr[idx2_loaded].metadata.inode, 67890);
}
