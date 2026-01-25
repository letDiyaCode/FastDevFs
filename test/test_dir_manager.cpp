#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <string>

#include "daemon/dir_manager.h"

/*
 * Tests for DirManager ADT.
 * Paths are treated as literal strings.
 * No canonicalization (/, //, trailing slash) is assumed.
 */

class DirManagerTest : public ::testing::Test {
protected:
    DirManager dm;

    void SetUp() override {
        dir_manager_init(&dm);
    }
};

/* ---------- Basic behavior ---------- */

TEST_F(DirManagerTest, RootExists) {
    EXPECT_EQ(lookup_node(&dm, "/"), 0);
}

TEST_F(DirManagerTest, InsertAndLookupSingleDir) {
    int node = insert_node(&dm, "/home");
    EXPECT_NE(node, -1);
    EXPECT_EQ(lookup_node(&dm, "/home"), node);
}

/* ---------- Nested paths ---------- */

TEST_F(DirManagerTest, InsertNestedDirectories) {
    EXPECT_NE(insert_node(&dm, "/home"), -1);
    EXPECT_NE(insert_node(&dm, "/home/user"), -1);
    EXPECT_NE(insert_node(&dm, "/home/user/docs"), -1);

    EXPECT_NE(lookup_node(&dm, "/home/user/docs"), -1);
}

TEST_F(DirManagerTest, InsertWithoutParentFails) {
    EXPECT_EQ(insert_node(&dm, "/a/b"), -1);
}

/* ---------- Duplicate & idempotency ---------- */

TEST_F(DirManagerTest, DuplicateInsertFails) {
    EXPECT_NE(insert_node(&dm, "/tmp"), -1);
    EXPECT_EQ(insert_node(&dm, "/tmp"), -1);
}

TEST_F(DirManagerTest, LookupIsIdempotent) {
    insert_node(&dm, "/x");
    int first = lookup_node(&dm, "/x");
    int second = lookup_node(&dm, "/x");
    EXPECT_EQ(first, second);
}

/* ---------- Removal semantics ---------- */

TEST_F(DirManagerTest, RemoveEmptyDirectory) {
    insert_node(&dm, "/var");
    EXPECT_TRUE(remove_node(&dm, "/var"));
    EXPECT_EQ(lookup_node(&dm, "/var"), -1);
}

TEST_F(DirManagerTest, RemoveNonEmptyDirectoryFails) {
    insert_node(&dm, "/etc");
    insert_node(&dm, "/etc/conf");

    EXPECT_FALSE(remove_node(&dm, "/etc"));
    EXPECT_NE(lookup_node(&dm, "/etc"), -1);
}

TEST_F(DirManagerTest, RemoveRootFails) {
    EXPECT_FALSE(remove_node(&dm, "/"));
}

/* ---------- Freelist reuse ---------- */

TEST_F(DirManagerTest, RemoveThenReinsert) {
    insert_node(&dm, "/tmp");
    EXPECT_TRUE(remove_node(&dm, "/tmp"));
    EXPECT_NE(insert_node(&dm, "/tmp"), -1);
}

/* ---------- Sibling integrity ---------- */

TEST_F(DirManagerTest, MultipleChildrenSameParent) {
    insert_node(&dm, "/p");
    insert_node(&dm, "/p/a");
    insert_node(&dm, "/p/b");
    insert_node(&dm, "/p/c");

    EXPECT_NE(lookup_node(&dm, "/p/a"), -1);
    EXPECT_NE(lookup_node(&dm, "/p/b"), -1);
    EXPECT_NE(lookup_node(&dm, "/p/c"), -1);
}

TEST_F(DirManagerTest, RemoveOneSiblingDoesNotAffectOthers) {
    insert_node(&dm, "/p");
    insert_node(&dm, "/p/a");
    insert_node(&dm, "/p/b");

    EXPECT_TRUE(remove_node(&dm, "/p/a"));
    EXPECT_EQ(lookup_node(&dm, "/p/a"), -1);
    EXPECT_NE(lookup_node(&dm, "/p/b"), -1);
}

/* ---------- Path edge cases (literal semantics) ---------- */

TEST_F(DirManagerTest, TrailingSlashIsDifferentPath) {
    insert_node(&dm, "/data");

    EXPECT_NE(lookup_node(&dm, "/data"), -1);
    EXPECT_EQ(lookup_node(&dm, "/data/"), -1);
}

TEST_F(DirManagerTest, MultipleSlashesAreDifferentPath) {
    insert_node(&dm, "/a");
    insert_node(&dm, "/a//b");

    EXPECT_NE(lookup_node(&dm, "/a//b"), -1);
    EXPECT_EQ(lookup_node(&dm, "/a/b"), -1);
}

TEST_F(DirManagerTest, EmptyPathFails) {
    EXPECT_EQ(insert_node(&dm, ""), -1);
    EXPECT_EQ(lookup_node(&dm, ""), -1);
}

/* ---------- Bounds & safety ---------- */

TEST_F(DirManagerTest, VeryLongNameHandledSafely) {
    std::string longName(300, 'a');
    std::string path = "/" + longName;

    // should not crash and should insert safely (name may be truncated)
    int node = insert_node(&dm, path.c_str());
    EXPECT_NE(node, -1);

    // lookup using same path string should succeed
    EXPECT_NE(lookup_node(&dm, path.c_str()), -1);
}


/* ---------- Transaction behavior ---------- */

TEST_F(DirManagerTest, FailedInsertIsAtomic) {
    insert_node(&dm, "/a");

    EXPECT_EQ(insert_node(&dm, "/a/b/c"), -1);

    EXPECT_NE(lookup_node(&dm, "/a"), -1);
    EXPECT_EQ(lookup_node(&dm, "/a/b"), -1);
}

/* ---------- Stress test ---------- */

TEST_F(DirManagerTest, ManyInsertsAndLookups) {
    const int N = 1000;
    std::vector<std::string> paths;

    for (int i = 0; i < N; i++) {
        paths.push_back("/dir" + std::to_string(i));
    }

    for (int i = 0; i < N; i++) {
        EXPECT_NE(insert_node(&dm, paths[i].c_str()), -1);
    }

    for (int i = 0; i < N; i++) {
        EXPECT_NE(lookup_node(&dm, paths[i].c_str()), -1);
    }
}

/* ---------- Concurrency ---------- */

TEST_F(DirManagerTest, ConcurrentReadersDoNotBlock) {
    insert_node(&dm, "/shared");

    auto reader = [&]() {
        for (int i = 0; i < 1000; i++) {
            EXPECT_NE(lookup_node(&dm, "/shared"), -1);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++)
        threads.emplace_back(reader);

    for (auto& t : threads)
        t.join();
}

TEST_F(DirManagerTest, ReaderWriterInterleavingSafe) {
    insert_node(&dm, "/x");

    auto reader = [&]() {
        for (int i = 0; i < 1000; i++)
            lookup_node(&dm, "/x");
    };

    auto writer = [&]() {
        insert_node(&dm, "/y");
    };

    std::thread t1(reader);
    std::thread t2(writer);

    t1.join();
    t2.join();

    EXPECT_NE(lookup_node(&dm, "/y"), -1);
}

TEST_F(DirManagerTest, ConcurrentWritersSafe) {
    auto writer = [&](const char* p) {
        insert_node(&dm, p);
    };

    std::thread t1(writer, "/a");
    std::thread t2(writer, "/b");

    t1.join();
    t2.join();

    EXPECT_NE(lookup_node(&dm, "/a"), -1);
    EXPECT_NE(lookup_node(&dm, "/b"), -1);
}


