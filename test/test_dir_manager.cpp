#include <gtest/gtest.h>
#include "daemon/dir_manager.h"

TEST(DirManagerTest, AddAndResolve) {
    DirManager dm;

    EXPECT_TRUE(dm.add_entry(0, "home", 1));
    EXPECT_TRUE(dm.add_entry(1, "file.txt", 2));

    EXPECT_EQ(dm.resolve_path("/home/file.txt"), 2);
}
