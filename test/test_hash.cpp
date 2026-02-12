#include <gtest/gtest.h>
#include "../include/daemon/directory tree/hash.h"

class HashTest : public ::testing::Test {
protected:
    HashMap* map;

    void SetUp() override {
        map = new HashMap();
    }

    void TearDown() override {
        delete map;
    }
};

TEST_F(HashTest, BasicOperations) {
    map->set("key1", 100);
    EXPECT_TRUE(map->has("key1"));
    EXPECT_EQ(map->get("key1"), 100);
    EXPECT_EQ((*map)["key1"], 100);
    
    map->remove("key1");
    EXPECT_FALSE(map->has("key1"));
}

TEST_F(HashTest, UpdateValue) {
    map->set("key2", 200);
    EXPECT_EQ(map->get("key2"), 200);
    
    map->set("key2", 300);
    EXPECT_EQ(map->get("key2"), 300);
}

TEST_F(HashTest, NonExistentKey) {
    EXPECT_FALSE(map->has("ghost"));
    EXPECT_EQ(map->get("ghost"), 0);
}

TEST_F(HashTest, MultipleKeys) {
    map->set("a", 1);
    map->set("b", 2);
    map->set("c", 3);
    
    EXPECT_EQ(map->get("a"), 1);
    EXPECT_EQ(map->get("b"), 2);
    EXPECT_EQ(map->get("c"), 3);
}

TEST_F(HashTest, OperatorAccess) {
    (*map)["newkey"] = 50;
    EXPECT_EQ(map->get("newkey"), 50);
    EXPECT_EQ((*map)["newkey"], 50);
}

TEST_F(HashTest, CollisionHandling) {
    // It's hard to force collision without knowing the hash function details intimately,
    // but we can insert enough items or specific patterns if known.
    // For now, let's just insert a few items.
    for(int i=0; i<100; ++i) {
        map->set("collision" + std::to_string(i), i);
    }
    for(int i=0; i<100; ++i) {
        EXPECT_EQ(map->get("collision" + std::to_string(i)), i);
    }
}

TEST_F(HashTest, RemoveNonExistent) {
    // Should return false or 0
    EXPECT_FALSE(map->remove("ghostKey"));
}

TEST_F(HashTest, UpdateNonExistent) {
    // Setting a non-existent key should create it
    map->set("new", 123);
    EXPECT_TRUE(map->has("new"));
    EXPECT_EQ(map->get("new"), 123);
}

TEST_F(HashTest, EmptyStringKey) {
    map->set("", 999);
    EXPECT_TRUE(map->has(""));
    EXPECT_EQ(map->get(""), 999);
}

TEST_F(HashTest, MaxLenKey) {
    std::string key(255, 'a');
    map->set(key, 777);
    EXPECT_TRUE(map->has(key));
    EXPECT_EQ(map->get(key), 777);
}

TEST_F(HashTest, SizeCheck) {
    size_t initial = map->size();
    map->set("one", 1);
    EXPECT_EQ(map->size(), initial + 1);
    
    map->remove("one");
    EXPECT_EQ(map->size(), initial);
}

TEST_F(HashTest, GetDefault) {
    EXPECT_EQ(map->get("nothing"), 0);
}

TEST_F(HashTest, ReassignOperator) {
    (*map)["assign"] = 10;
    EXPECT_EQ(map->get("assign"), 10);
    (*map)["assign"] = 20;
    EXPECT_EQ(map->get("assign"), 20);
}

TEST_F(HashTest, RemoveAndReadd) {
    map->set("temp", 5);
    map->remove("temp");
    EXPECT_FALSE(map->has("temp"));
    map->set("temp", 5);
    EXPECT_TRUE(map->has("temp"));
}

TEST_F(HashTest, ManyInserts) {
    for(int i=0; i<500; ++i) {
        map->set("mi_" + std::to_string(i), i);
    }
    EXPECT_GE(map->size(), 500);
    EXPECT_EQ(map->get("mi_0"), 0);
    EXPECT_EQ(map->get("mi_499"), 499);
}

TEST_F(HashTest, HasAfterRemove) {
    map->set("exists", 1);
    EXPECT_TRUE(map->has("exists"));
    map->remove("exists");
    EXPECT_FALSE(map->has("exists"));
}
