#include <gtest/gtest.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <set>
#include "../include/daemon/directory tree/adt.h"

// ============================================================
// Test fixture — fresh mmap'd treefile per test
// ============================================================

class NodeShareTest : public ::testing::Test {
protected:
    treefile* tf;
    int fd;
    size_t mapsize;
    const char* test_path = "/tmp/test_node_share.mmap";

    void SetUp() override {
        unlink(test_path);
        tf = nullptr;
        fd = -1;
        mapsize = 0;
        ASSERT_TRUE(mmap_init_treefile(test_path, tf, fd, mapsize));
    }

    void TearDown() override {
        mmap_close_treefile(tf, fd, mapsize);
        unlink(test_path);
    }

    // Helper: build a library folder with N files under it
    void build_folder(const std::string& path, int num_files) {
        // Ensure parent exists — split path
        std::string parent = "/";
        size_t pos = path.rfind('/');
        if (pos != std::string::npos && pos > 0) {
            parent = path.substr(0, pos);
        }
        insertfolder(path, parent, *tf);
        int dir_idx = hashindex(path, *tf);
        ASSERT_NE(dir_idx, -1) << "Failed to create folder: " << path;

        for (int i = 0; i < num_files; i++) {
            std::string fname = path + "/file" + std::to_string(i) + ".js";
            insertfile(fname, path, *tf);
            ASSERT_NE(hashindex(fname, *tf), -1) << "Failed to create: " << fname;

            // Fake some metadata so files look real
            int fidx = hashindex(fname, *tf);
            tf->arr[fidx].metadata.size = 1000 + i;
            tf->arr[fidx].metadata.mode = S_IFREG | 0644;
        }
    }

    // Helper: count non-deleted children of a directory
    int count_children(int dir_idx) {
        int count = 0;
        int child = tf->arr[dir_idx].firstchild;
        int guard = tf->size;
        while (child >= 0 && child < tf->size && guard-- > 0) {
            if (!tf->arr[child].isdeleted) count++;
            child = tf->arr[child].nextsibling;
        }
        return count;
    }

    // Helper: collect child basenames
    std::vector<std::string> child_names(int dir_idx) {
        std::vector<std::string> names;
        int child = tf->arr[dir_idx].firstchild;
        int guard = tf->size;
        while (child >= 0 && child < tf->size && guard-- > 0) {
            if (!tf->arr[child].isdeleted) {
                std::string full = tf->arr[child].metadata.name;
                size_t slash = full.rfind('/');
                if (slash != std::string::npos)
                    names.push_back(full.substr(slash + 1));
                else
                    names.push_back(full);
            }
            child = tf->arr[child].nextsibling;
        }
        return names;
    }
};

// ============================================================
// Basic: dedup_link shares firstchild
// ============================================================

TEST_F(NodeShareTest, BasicLink) {
    build_folder("/proj1/lodash", 5);
    insertfolder("/proj2", "/", *tf);
    insertfolder("/proj2/lodash", "/proj2", *tf);

    int canon = hashindex("/proj1/lodash", *tf);
    int alias = hashindex("/proj2/lodash", *tf);
    ASSERT_NE(canon, -1);
    ASSERT_NE(alias, -1);

    // Alias starts empty
    EXPECT_EQ(count_children(alias), 0);

    // Link
    dedup_link(alias, canon, *tf);

    // Now alias shares canonical's firstchild
    EXPECT_EQ(tf->arr[alias].firstchild, tf->arr[canon].firstchild);
    EXPECT_TRUE(tf->arr[alias].is_deduped);
    EXPECT_EQ(tf->arr[alias].dedup_source, canon);
    EXPECT_EQ(tf->arr[canon].dedup_refcount, 2);

    // Both should show 5 children
    EXPECT_EQ(count_children(alias), 5);
    EXPECT_EQ(count_children(canon), 5);
}

// ============================================================
// Basic: dedup_break creates a private chain
// ============================================================

TEST_F(NodeShareTest, BasicBreak) {
    build_folder("/proj1/lodash", 5);
    insertfolder("/proj2", "/", *tf);
    insertfolder("/proj2/lodash", "/proj2", *tf);

    int canon = hashindex("/proj1/lodash", *tf);
    int alias = hashindex("/proj2/lodash", *tf);

    dedup_link(alias, canon, *tf);

    // Break
    dedup_break(alias, *tf);

    EXPECT_FALSE(tf->arr[alias].is_deduped);
    EXPECT_EQ(tf->arr[alias].dedup_source, -1);
    EXPECT_EQ(tf->arr[canon].dedup_refcount, 1);

    // Alias should have its own 5-child chain (different indices)
    EXPECT_NE(tf->arr[alias].firstchild, tf->arr[canon].firstchild);
    EXPECT_EQ(count_children(alias), 5);
    EXPECT_EQ(count_children(canon), 5);
}

// ============================================================
// After break, alias children have correct paths
// ============================================================

TEST_F(NodeShareTest, BreakRemapsPaths) {
    insertfolder("/proj1", "/", *tf);
    build_folder("/proj1/lodash", 3);
    insertfolder("/proj2", "/", *tf);
    insertfolder("/proj2/lodash", "/proj2", *tf);

    int canon = hashindex("/proj1/lodash", *tf);
    int alias = hashindex("/proj2/lodash", *tf);

    dedup_link(alias, canon, *tf);
    dedup_break(alias, *tf);

    // The alias's children should have /proj2/lodash/... paths
    auto names = child_names(alias);
    EXPECT_EQ(names.size(), 3u);
    for (const auto& n : names) {
        EXPECT_TRUE(n.find("file") != std::string::npos) << "Unexpected name: " << n;
    }

    // Verify full paths via hashmap
    for (int i = 0; i < 3; i++) {
        std::string expected_path = "/proj2/lodash/file" + std::to_string(i) + ".js";
        int idx = hashindex(expected_path, *tf);
        EXPECT_NE(idx, -1) << "Missing: " << expected_path;
        if (idx >= 0) {
            EXPECT_EQ(tf->arr[idx].parent, alias);
        }
    }
}

// ============================================================
// Mutation after break doesn't affect canonical
// ============================================================

TEST_F(NodeShareTest, MutationAfterBreak) {
    insertfolder("/proj1", "/", *tf);
    build_folder("/proj1/lodash", 3);
    insertfolder("/proj2", "/", *tf);
    insertfolder("/proj2/lodash", "/proj2", *tf);

    int canon = hashindex("/proj1/lodash", *tf);
    int alias = hashindex("/proj2/lodash", *tf);

    dedup_link(alias, canon, *tf);
    dedup_break(alias, *tf);

    // Add a file to alias — should NOT appear in canonical
    insertfile("/proj2/lodash/extra.js", "/proj2/lodash", *tf);
    EXPECT_EQ(count_children(alias), 4);
    EXPECT_EQ(count_children(canon), 3); // canonical untouched

    // Delete a file from alias — should NOT affect canonical
    delete1("/proj2/lodash/file0.js", *tf);
    EXPECT_EQ(count_children(alias), 3);
    EXPECT_EQ(count_children(canon), 3);
}

// ============================================================
// Self-link is a no-op
// ============================================================

TEST_F(NodeShareTest, SelfLinkNoop) {
    build_folder("/proj1/lodash", 3);
    int canon = hashindex("/proj1/lodash", *tf);
    int old_fc = tf->arr[canon].firstchild;
    int old_ref = tf->arr[canon].dedup_refcount;

    dedup_link(canon, canon, *tf);

    EXPECT_EQ(tf->arr[canon].firstchild, old_fc);
    EXPECT_EQ(tf->arr[canon].dedup_refcount, old_ref);
    EXPECT_FALSE(tf->arr[canon].is_deduped);
}

// ============================================================
// Break on a non-deduped dir is a no-op
// ============================================================

TEST_F(NodeShareTest, BreakNonDedupedNoop) {
    build_folder("/proj1/lodash", 3);
    int canon = hashindex("/proj1/lodash", *tf);
    int old_fc = tf->arr[canon].firstchild;

    dedup_break(canon, *tf);

    EXPECT_EQ(tf->arr[canon].firstchild, old_fc);
    EXPECT_FALSE(tf->arr[canon].is_deduped);
}

// ============================================================
// Multiple aliases sharing the same canonical
// ============================================================

TEST_F(NodeShareTest, MultipleAliases) {
    insertfolder("/proj1", "/", *tf);
    build_folder("/proj1/lodash", 4);
    int canon = hashindex("/proj1/lodash", *tf);

    // Create 5 aliases
    for (int i = 2; i <= 6; i++) {
        std::string proj = "/proj" + std::to_string(i);
        insertfolder(proj, "/", *tf);
        std::string dir = proj + "/lodash";
        insertfolder(dir, proj, *tf);

        int alias = hashindex(dir, *tf);
        ASSERT_NE(alias, -1);
        dedup_link(alias, canon, *tf);
    }

    EXPECT_EQ(tf->arr[canon].dedup_refcount, 6); // 1 canonical + 5 aliases

    // All aliases show 4 children
    for (int i = 2; i <= 6; i++) {
        std::string dir = "/proj" + std::to_string(i) + "/lodash";
        int alias = hashindex(dir, *tf);
        EXPECT_EQ(count_children(alias), 4);
        EXPECT_TRUE(tf->arr[alias].is_deduped);
    }

    // Break one alias
    int a3 = hashindex("/proj3/lodash", *tf);
    dedup_break(a3, *tf);
    EXPECT_EQ(tf->arr[canon].dedup_refcount, 5);
    EXPECT_FALSE(tf->arr[a3].is_deduped);

    // Others still linked
    for (int i : {2, 4, 5, 6}) {
        std::string dir = "/proj" + std::to_string(i) + "/lodash";
        int alias = hashindex(dir, *tf);
        EXPECT_TRUE(tf->arr[alias].is_deduped);
    }
}

// ============================================================
// Repeated link→break cycles (stress)
// ============================================================

TEST_F(NodeShareTest, RepeatedLinkBreakCycles) {
    insertfolder("/proj1", "/", *tf);
    build_folder("/proj1/lodash", 5);
    int canon = hashindex("/proj1/lodash", *tf);

    insertfolder("/proj2", "/", *tf);
    insertfolder("/proj2/lodash", "/proj2", *tf);
    int alias = hashindex("/proj2/lodash", *tf);

    for (int cycle = 0; cycle < 50; cycle++) {
        // Link
        dedup_link(alias, canon, *tf);
        EXPECT_TRUE(tf->arr[alias].is_deduped) << "cycle=" << cycle;
        EXPECT_EQ(count_children(alias), 5) << "cycle=" << cycle;

        // Break
        dedup_break(alias, *tf);
        EXPECT_FALSE(tf->arr[alias].is_deduped) << "cycle=" << cycle;
        EXPECT_EQ(count_children(alias), 5) << "cycle=" << cycle;
        EXPECT_EQ(count_children(canon), 5) << "cycle=" << cycle;

        // Clean up alias's private children for next cycle
        // (Otherwise each break allocates new nodes and we eventually run out)
        int child = tf->arr[alias].firstchild;
        std::vector<std::string> to_delete;
        int guard = tf->size;
        while (child >= 0 && child < tf->size && guard-- > 0) {
            if (!tf->arr[child].isdeleted) {
                to_delete.push_back(tf->arr[child].metadata.name);
            }
            child = tf->arr[child].nextsibling;
        }
        for (const auto& path : to_delete) {
            delete1(path, *tf);
        }
        EXPECT_EQ(count_children(alias), 0) << "cycle=" << cycle;
    }

    // Canonical should still be intact after 50 cycles
    EXPECT_EQ(count_children(canon), 5);
    EXPECT_EQ(tf->arr[canon].dedup_refcount, 1);
}

// ============================================================
// Stress: 100 aliases, break them all, verify isolation
// ============================================================

TEST_F(NodeShareTest, Stress100Aliases) {
    // Canonical with 10 files
    insertfolder("/canonical", "/", *tf);
    build_folder("/canonical/lib", 10);
    int canon = hashindex("/canonical/lib", *tf);
    ASSERT_NE(canon, -1);

    // Create 100 aliases
    std::vector<int> alias_indices;
    for (int i = 0; i < 100; i++) {
        std::string proj = "/p" + std::to_string(i);
        insertfolder(proj, "/", *tf);
        std::string dir = proj + "/lib";
        insertfolder(dir, proj, *tf);

        int alias = hashindex(dir, *tf);
        ASSERT_NE(alias, -1) << "Failed at alias i=" << i;
        dedup_link(alias, canon, *tf);
        alias_indices.push_back(alias);
    }

    EXPECT_EQ(tf->arr[canon].dedup_refcount, 101);

    // All 100 see 10 children
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(count_children(alias_indices[i]), 10) << "alias i=" << i;
    }

    // Break all of them
    for (int i = 0; i < 100; i++) {
        dedup_break(alias_indices[i], *tf);
        EXPECT_FALSE(tf->arr[alias_indices[i]].is_deduped) << "alias i=" << i;
        EXPECT_EQ(count_children(alias_indices[i]), 10) << "alias i=" << i;
    }

    EXPECT_EQ(tf->arr[canon].dedup_refcount, 1);
    EXPECT_EQ(count_children(canon), 10);
}

// ============================================================
// Stress: rapid insert-into-deduped-dir forces CoW breaks
// ============================================================

TEST_F(NodeShareTest, StressInsertIntoDeduped) {
    insertfolder("/proj1", "/", *tf);
    build_folder("/proj1/lib", 5);
    int canon = hashindex("/proj1/lib", *tf);

    // Create 20 aliases and insert into each (forcing break)
    for (int i = 0; i < 20; i++) {
        std::string proj = "/q" + std::to_string(i);
        insertfolder(proj, "/", *tf);
        std::string dir = proj + "/lib";
        insertfolder(dir, proj, *tf);

        int alias = hashindex(dir, *tf);
        dedup_link(alias, canon, *tf);

        // Insert into deduped dir → must break first
        ASSERT_TRUE(tf->arr[alias].is_deduped);
        dedup_break(alias, *tf);
        ASSERT_FALSE(tf->arr[alias].is_deduped);

        std::string new_file = dir + "/patch" + std::to_string(i) + ".js";
        insertfile(new_file, dir, *tf);
        EXPECT_NE(hashindex(new_file, *tf), -1);

        // Alias should have 6 children, canonical still 5
        EXPECT_EQ(count_children(alias), 6) << "alias i=" << i;
        EXPECT_EQ(count_children(canon), 5) << "canonical polluted at i=" << i;
    }
}

// ============================================================
// Stress: node count stays bounded — free list works
// ============================================================

TEST_F(NodeShareTest, StressNodeCount) {
    int initial_alloc = tf->nodeallocated;

    insertfolder("/proj1", "/", *tf);
    build_folder("/proj1/lib", 10);
    int after_canon = tf->nodeallocated;

    // Create alias, link (should allocate 1 dir node only)
    insertfolder("/proj2", "/", *tf);
    insertfolder("/proj2/lib", "/proj2", *tf);
    int after_alias_dir = tf->nodeallocated;

    int canon = hashindex("/proj1/lib", *tf);
    int alias = hashindex("/proj2/lib", *tf);
    dedup_link(alias, canon, *tf);

    // dedup_link should NOT allocate any extra nodes
    EXPECT_EQ(tf->nodeallocated, after_alias_dir);

    // Break allocates up to 10 copies (one per child)
    dedup_break(alias, *tf);
    int after_break = tf->nodeallocated;
    EXPECT_LE(after_break - after_alias_dir, 10);

    // Delete alias subtree and alias dir
    int child = tf->arr[alias].firstchild;
    std::vector<std::string> to_del;
    int guard = tf->size;
    while (child >= 0 && child < tf->size && guard-- > 0) {
        if (!tf->arr[child].isdeleted)
            to_del.push_back(tf->arr[child].metadata.name);
        child = tf->arr[child].nextsibling;
    }
    for (const auto& p : to_del) delete1(p, *tf);
    delete1("/proj2/lib", *tf);
    delete1("/proj2", *tf);

    // Free list should have reclaimed those nodes
    int free_count = 0;
    int f = tf->firstfree;
    guard = tf->size;
    while (f >= 0 && f < tf->size && guard-- > 0) {
        free_count++;
        f = tf->arr[f].nextfree;
    }
    // At least 12 nodes freed: 10 children + /proj2/lib + /proj2
    EXPECT_GE(free_count, 12);
}

// ============================================================
// Edge case: link to an empty canonical
// ============================================================

TEST_F(NodeShareTest, LinkToEmptyCanonical) {
    insertfolder("/proj1", "/", *tf);
    insertfolder("/proj1/empty", "/proj1", *tf);
    insertfolder("/proj2", "/", *tf);
    insertfolder("/proj2/empty", "/proj2", *tf);

    int canon = hashindex("/proj1/empty", *tf);
    int alias = hashindex("/proj2/empty", *tf);

    dedup_link(alias, canon, *tf);
    EXPECT_EQ(tf->arr[alias].firstchild, -1); // canonical is empty
    EXPECT_TRUE(tf->arr[alias].is_deduped);

    // Break an empty share — should be a no-op essentially
    dedup_break(alias, *tf);
    EXPECT_FALSE(tf->arr[alias].is_deduped);
    EXPECT_EQ(tf->arr[alias].firstchild, -1);
}

// ============================================================
// Edge case: delete a deduped alias dir (refcount cleanup)
// ============================================================

TEST_F(NodeShareTest, DeleteDedupedAlias) {
    insertfolder("/proj1", "/", *tf);
    build_folder("/proj1/lib", 3);
    insertfolder("/proj2", "/", *tf);
    insertfolder("/proj2/lib", "/proj2", *tf);

    int canon = hashindex("/proj1/lib", *tf);
    int alias = hashindex("/proj2/lib", *tf);

    dedup_link(alias, canon, *tf);
    EXPECT_EQ(tf->arr[canon].dedup_refcount, 2);

    // Simulate what ll_rmdir does: clear shared state before delete
    tf->arr[alias].firstchild = -1;
    tf->arr[alias].is_deduped = false;
    tf->arr[alias].dedup_source = -1;
    tf->arr[canon].dedup_refcount--;

    delete1("/proj2/lib", *tf);

    // Canonical untouched, refcount back to 1
    EXPECT_EQ(tf->arr[canon].dedup_refcount, 1);
    EXPECT_EQ(count_children(canon), 3);
    EXPECT_EQ(hashindex("/proj2/lib", *tf), -1);
}
