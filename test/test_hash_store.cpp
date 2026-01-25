#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <chrono>
#include <iostream>

#include <cstring>

#include "daemon/hash.h"

/*
 * Tests for static hash table.
 * Hash is treated as an index; key is the identity.
 * Collisions are resolved by chaining + key comparison.
 */

class HashStoreTest : public ::testing::Test {
protected:
    HashTable ht;

    void SetUp() override {
        hash_init(&ht);
    }
};

/* ---------- Basic behavior ---------- */

TEST_F(HashStoreTest, InsertLookupSingle) {
    const char* key = "file";
    uint64_t h = hash_combine(0, key);

    EXPECT_TRUE(hash_insert(&ht, h, key, 10));
    EXPECT_EQ(hash_lookup(&ht, h, key), 10);
}

TEST_F(HashStoreTest, LookupNonExistingReturnsMinusOne) {
    const char* key = "ghost";
    uint64_t h = hash_combine(0, key);

    EXPECT_EQ(hash_lookup(&ht, h, key), -1);
}

/* ---------- Duplicate handling ---------- */

TEST_F(HashStoreTest, DuplicateInsertAllowedOrRejectedButSafe) {
    const char* key = "dup";
    uint64_t h = hash_combine(0, key);

    EXPECT_TRUE(hash_insert(&ht, h, key, 1));

    bool second = hash_insert(&ht, h, key, 2);

    if (second) {
        int v = hash_lookup(&ht, h, key);
        EXPECT_TRUE(v == 1 || v == 2);
    } else {
        EXPECT_EQ(hash_lookup(&ht, h, key), 1);
    }
}

/* ---------- Remove semantics ---------- */

TEST_F(HashStoreTest, RemoveExistingEntry) {
    const char* key = "tmp";
    uint64_t h = hash_combine(0, key);

    EXPECT_TRUE(hash_insert(&ht, h, key, 5));
    EXPECT_TRUE(hash_remove(&ht, h, key));
    EXPECT_EQ(hash_lookup(&ht, h, key), -1);
}

TEST_F(HashStoreTest, RemoveNonExistingFailsGracefully) {
    const char* key = "missing";
    uint64_t h = hash_combine(0, key);

    EXPECT_FALSE(hash_remove(&ht, h, key));
}

/* ---------- Freelist reuse ---------- */

TEST_F(HashStoreTest, RemoveThenReinsertUsesSlotSafely) {
    const char* key1 = "a";
    const char* key2 = "b";

    uint64_t h1 = hash_combine(0, key1);
    uint64_t h2 = hash_combine(0, key2);

    EXPECT_TRUE(hash_insert(&ht, h1, key1, 1));
    EXPECT_TRUE(hash_remove(&ht, h1, key1));
    EXPECT_TRUE(hash_insert(&ht, h2, key2, 2));

    EXPECT_EQ(hash_lookup(&ht, h1, key1), -1);
    EXPECT_EQ(hash_lookup(&ht, h2, key2), 2);
}

/* ---------- Collision behavior ---------- */

TEST_F(HashStoreTest, HandlesBucketCollisionsCorrectly) {
    std::vector<std::string> keys;
    std::vector<uint64_t> hashes;

    // force same bucket, different keys
    for (int i = 0; i < 50; i++) {
        keys.push_back("key_" + std::to_string(i));
        hashes.push_back(i * HASH_SIZE);
    }

    for (int i = 0; i < 50; i++) {
        EXPECT_TRUE(hash_insert(&ht, hashes[i], keys[i].c_str(), i));
    }

    for (int i = 0; i < 50; i++) {
        EXPECT_EQ(hash_lookup(&ht, hashes[i], keys[i].c_str()), i);
    }
}

/* ---------- Incremental hashing ---------- */

TEST_F(HashStoreTest, HashCombineIsOrderSensitive) {
    uint64_t h1 = hash_combine(0, "a");
    h1 = hash_combine(h1, "b");

    uint64_t h2 = hash_combine(0, "b");
    h2 = hash_combine(h2, "a");

    EXPECT_NE(h1, h2);
}

TEST_F(HashStoreTest, HashCombineSeparatesComponents) {
    uint64_t h1 = hash_combine(0, "ab");

    uint64_t h2 = hash_combine(0, "a");
    h2 = hash_combine(h2, "b");

    EXPECT_NE(h1, h2);
}

/* ---------- Stress ---------- */

TEST_F(HashStoreTest, ManyInsertsAndLookups) {
    const int N = 2000;
    std::vector<std::string> keys;
    std::vector<uint64_t> hashes;

    for (int i = 0; i < N; i++) {
        keys.push_back("key_" + std::to_string(i));
        hashes.push_back(hash_combine(0, keys[i].c_str()));
    }

    int inserted = 0;
    for (int i = 0; i < N; i++) {
        if (hash_insert(&ht, hashes[i], keys[i].c_str(), i))
            inserted++;
    }

    // sanity: most inserts should succeed
    EXPECT_GT(inserted, N * 0.9);

    // correctness: lookup(key) == value
    for (int i = 0; i < N; i++) {
        int v = hash_lookup(&ht, hashes[i], keys[i].c_str());
        if (v != -1)
            EXPECT_EQ(v, i);
    }
}

/* ---------- Capacity limits ---------- */

TEST_F(HashStoreTest, TableEventuallyFillsUp) {
    int success = 0;

    for (int i = 0; i < MAX_ENTRIES + 10; i++) {
        std::string key = "k" + std::to_string(i);
        uint64_t h = hash_combine(0, key.c_str());

        if (hash_insert(&ht, h, key.c_str(), i))
            success++;
    }

    EXPECT_EQ(success, MAX_ENTRIES);
}


TEST_F(HashStoreTest, HashCombinePerformance) {
    const int ITERATIONS = 100;
    const char* key = "very_long_directory_name_for_benchmarking";

    uint64_t h = 0;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < ITERATIONS; i++) {
        h = hash_combine(h, key);
    }

    auto end = std::chrono::steady_clock::now();

    std::chrono::duration<double, std::micro> elapsed = end - start;

    double avg_time = elapsed.count() / ITERATIONS;

    // Print results (do NOT assert on timing)
    std::cout << "\n[ HASH BENCHMARK ]\n";
    std::cout << "Iterations: " << ITERATIONS << "\n";
    std::cout << "Total time (µs): " << elapsed.count() << "\n";
    std::cout << "Avg time per hash (µs): " << avg_time << "\n";
    std::cout << "Final hash (prevent opt): " << h << "\n";

    SUCCEED(); // benchmark only
}
