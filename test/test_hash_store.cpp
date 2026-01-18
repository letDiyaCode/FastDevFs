#include <gtest/gtest.h>
#include "daemon/hash_store.h"

TEST(HashStoreTest, InsertAndVerify) {
    HashStore hs;

    EXPECT_TRUE(hs.insert(10, "hello"));
    EXPECT_TRUE(hs.verify(10, "hello"));
    EXPECT_FALSE(hs.verify(10, "world"));
}
