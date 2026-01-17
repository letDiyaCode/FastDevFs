#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "daemon/directory tree/hash.h"

using namespace std;

// Fixture class for Hash map tests
class HashMapTest : public ::testing::Test {
protected:
    void SetUp() override {
        hash = new HashMap();
    }
    
    void TearDown() override {
        delete hash;
    }
    
    HashMap* hash;
};

// Test 1: Create and destroy hash map
TEST_F(HashMapTest, CreateAndDestroy) {
    EXPECT_NE(hash, nullptr);
    EXPECT_EQ(hash->size(), 0);
}

// Test 2: Insert and get value
TEST_F(HashMapTest, InsertAndGet) {
    (*hash)["key1"] = 42;
    EXPECT_EQ((*hash)["key1"], 42);
    EXPECT_EQ(hash->size(), 1);
}

// Test 3: Has function
TEST_F(HashMapTest, HasFunction) {
    EXPECT_FALSE(hash->has("key1"));
    (*hash)["key1"] = 10;
    EXPECT_TRUE(hash->has("key1"));
    EXPECT_FALSE(hash->has("key2"));
}

// Test 4: Set and get
TEST_F(HashMapTest, SetAndGet) {
    hash->set("key1", 100);
    EXPECT_EQ(hash->get("key1"), 100);
    EXPECT_EQ(hash->size(), 1);
}

// Test 5: Remove entry
TEST_F(HashMapTest, RemoveEntry) {
    (*hash)["key1"] = 50;
    (*hash)["key2"] = 75;
    
    EXPECT_EQ(hash->size(), 2);
    EXPECT_TRUE(hash->has("key1"));
    
    bool removed = hash->remove("key1");
    EXPECT_TRUE(removed);
    EXPECT_FALSE(hash->has("key1"));
    EXPECT_TRUE(hash->has("key2"));
    EXPECT_EQ(hash->size(), 1);
}

// Test 6: Remove non-existent key
TEST_F(HashMapTest, RemoveNonExistentKey) {
    bool removed = hash->remove("nonexistent");
    EXPECT_FALSE(removed);
    EXPECT_EQ(hash->size(), 0);
}

// Test 7: Update existing value
TEST_F(HashMapTest, UpdateExistingValue) {
    (*hash)["key1"] = 10;
    EXPECT_EQ((*hash)["key1"], 10);
    
    (*hash)["key1"] = 20;
    EXPECT_EQ((*hash)["key1"], 20);
    EXPECT_EQ(hash->size(), 1); // Size should not increase
}

// Test 8: Multiple keys
TEST_F(HashMapTest, MultipleKeys) {
    for (int i = 0; i < 100; i++) {
        string key = "key" + to_string(i);
        (*hash)[key] = i;
    }
    
    EXPECT_EQ(hash->size(), 100);
    
    // Verify some values
    EXPECT_EQ((*hash)["key0"], 0);
    EXPECT_EQ((*hash)["key50"], 50);
    EXPECT_EQ((*hash)["key99"], 99);
}

// Test 9: Get non-existent key returns default
TEST_F(HashMapTest, GetNonExistentReturnsDefault) {
    EXPECT_EQ(hash->get("nonexistent"), 0);
    EXPECT_EQ((*hash)["nonexistent"], 0); // Should create entry
    EXPECT_EQ(hash->size(), 1);
}

// Test 10: Clear by removing all keys
TEST_F(HashMapTest, ClearByRemovingAll) {
    for (int i = 0; i < 10; i++) {
        (*hash)["key" + to_string(i)] = i;
    }
    
    EXPECT_EQ(hash->size(), 10);
    
    // Remove all
    for (int i = 0; i < 10; i++) {
        hash->remove("key" + to_string(i));
    }
    
    EXPECT_EQ(hash->size(), 0);
}

// Test 11: String keys of various lengths
TEST_F(HashMapTest, VariousStringLengths) {
    (*hash)[""] = 0; // Empty string
    (*hash)["a"] = 1;
    (*hash)["ab"] = 2;
    (*hash)["abc"] = 3;
    (*hash)["very long key name for testing purposes"] = 100;
    
    EXPECT_EQ(hash->size(), 5);
    EXPECT_EQ((*hash)[""], 0);
    EXPECT_EQ((*hash)["a"], 1);
    EXPECT_EQ((*hash)["very long key name for testing purposes"], 100);
}

// Test 12: Collision handling
TEST_F(HashMapTest, CollisionHandling) {
    // Insert many keys to potentially cause collisions
    for (int i = 0; i < 1000; i++) {
        string key = "key_" + to_string(i);
        (*hash)[key] = i * 2;
    }
    
    EXPECT_EQ(hash->size(), 1000);
    
    // Verify all keys are still accessible
    for (int i = 0; i < 1000; i++) {
        string key = "key_" + to_string(i);
        EXPECT_EQ((*hash)[key], i * 2);
    }
}

// Test 13: Operator[] creates entry
TEST_F(HashMapTest, OperatorBracketCreatesEntry) {
    EXPECT_FALSE(hash->has("newkey"));
    int& value = (*hash)["newkey"];
    value = 42;
    
    EXPECT_TRUE(hash->has("newkey"));
    EXPECT_EQ((*hash)["newkey"], 42);
}

// Test 14: Size increases correctly
TEST_F(HashMapTest, SizeIncreasesCorrectly) {
    EXPECT_EQ(hash->size(), 0);
    
    for (int i = 1; i <= 50; i++) {
        (*hash)["key" + to_string(i)] = i;
        EXPECT_EQ(hash->size(), i);
    }
}

