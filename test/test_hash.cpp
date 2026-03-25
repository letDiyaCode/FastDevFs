#include <gtest/gtest.h>
#include <string>
#include "../include/daemon/directory tree/hash.h"

class HashTest : public ::testing::Test {
protected:
    hashmap_t* map;

    void SetUp() override {
        map = hashmap_create(0);
    }

    void TearDown() override {
        hashmap_destroy(map);
    }
};

TEST_F(HashTest, BasicOperations) {
    hashmap_set(map, "key1", 100);
    EXPECT_TRUE(hashmap_has(map, "key1"));
    EXPECT_EQ(hashmap_get(map, "key1"), 100);

    hashmap_remove(map, "key1");
    EXPECT_FALSE(hashmap_has(map, "key1"));
}

TEST_F(HashTest, UpdateValue) {
    hashmap_set(map, "key2", 200);
    EXPECT_EQ(hashmap_get(map, "key2"), 200);

    hashmap_set(map, "key2", 300);
    EXPECT_EQ(hashmap_get(map, "key2"), 300);
}

TEST_F(HashTest, NonExistentKey) {
    EXPECT_FALSE(hashmap_has(map, "ghost"));
    EXPECT_EQ(hashmap_get(map, "ghost"), 0);
}

TEST_F(HashTest, MultipleKeys) {
    hashmap_set(map, "a", 1);
    hashmap_set(map, "b", 2);
    hashmap_set(map, "c", 3);

    EXPECT_EQ(hashmap_get(map, "a"), 1);
    EXPECT_EQ(hashmap_get(map, "b"), 2);
    EXPECT_EQ(hashmap_get(map, "c"), 3);
}

TEST_F(HashTest, RefAccess) {
    int* ref = hashmap_ref(map, "newkey");
    ASSERT_NE(ref, nullptr);
    *ref = 50;
    EXPECT_EQ(hashmap_get(map, "newkey"), 50);
}

TEST_F(HashTest, CollisionHandling) {
    for (int i = 0; i < 100; ++i) {
        hashmap_set(map, ("collision" + std::to_string(i)).c_str(), i);
    }
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(hashmap_get(map, ("collision" + std::to_string(i)).c_str()), i);
    }
}

TEST_F(HashTest, RemoveNonExistent) {
    EXPECT_EQ(hashmap_remove(map, "ghostKey"), 0);
}

TEST_F(HashTest, UpdateNonExistent) {
    hashmap_set(map, "new", 123);
    EXPECT_TRUE(hashmap_has(map, "new"));
    EXPECT_EQ(hashmap_get(map, "new"), 123);
}

TEST_F(HashTest, EmptyStringKey) {
    hashmap_set(map, "", 999);
    EXPECT_TRUE(hashmap_has(map, ""));
    EXPECT_EQ(hashmap_get(map, ""), 999);
}

TEST_F(HashTest, MaxLenKey) {
    std::string key(255, 'a');
    hashmap_set(map, key.c_str(), 777);
    EXPECT_TRUE(hashmap_has(map, key.c_str()));
    EXPECT_EQ(hashmap_get(map, key.c_str()), 777);
}

TEST_F(HashTest, SizeCheck) {
    size_t initial = hashmap_size(map);
    hashmap_set(map, "one", 1);
    EXPECT_EQ(hashmap_size(map), initial + 1);

    hashmap_remove(map, "one");
    EXPECT_EQ(hashmap_size(map), initial);
}

TEST_F(HashTest, GetDefault) {
    EXPECT_EQ(hashmap_get(map, "nothing"), 0);
}

TEST_F(HashTest, RemoveAndReadd) {
    hashmap_set(map, "temp", 5);
    hashmap_remove(map, "temp");
    EXPECT_FALSE(hashmap_has(map, "temp"));
    hashmap_set(map, "temp", 5);
    EXPECT_TRUE(hashmap_has(map, "temp"));
}

TEST_F(HashTest, ManyInserts) {
    for (int i = 0; i < 500; ++i) {
        hashmap_set(map, ("mi_" + std::to_string(i)).c_str(), i);
    }
    EXPECT_GE(hashmap_size(map), (size_t)500);
    EXPECT_EQ(hashmap_get(map, "mi_0"), 0);
    EXPECT_EQ(hashmap_get(map, "mi_499"), 499);
}

TEST_F(HashTest, HasAfterRemove) {
    hashmap_set(map, "exists", 1);
    EXPECT_TRUE(hashmap_has(map, "exists"));
    hashmap_remove(map, "exists");
    EXPECT_FALSE(hashmap_has(map, "exists"));
}
