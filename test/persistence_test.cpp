#include <gtest/gtest.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

#include "daemon/dir_manager.h"
#include "daemon/hash.h"

/*
 * System-level persistence tests.
 *
 * Verifies that DirManager and HashTable survive restart
 * when backed by a file using mmap.
 *
 * NOTE:
 * - pthread locks are NOT persistent and must be re-initialized
 * - structural data (nodes, hash entries) must survive
 */

static const char* PERSIST_FILE = "/tmp/fastdevfs_persist_test.dat";

/* ---------------------------------------------------------
 * Helper: map a file of given size
 * --------------------------------------------------------- */
static void* map_file(const char* path, size_t size, int& fd) {
    fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd == -1)
        return MAP_FAILED;

    if (ftruncate(fd, size) != 0)
        return MAP_FAILED;

    return mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
}

/* ---------------------------------------------------------
 * DirManager persistence
 * --------------------------------------------------------- */
TEST(PersistenceTest, DirManagerPersistsAcrossRestart) {
    int fd;
    void* addr = map_file(PERSIST_FILE, sizeof(DirManager), fd);
    ASSERT_NE(addr, MAP_FAILED);

    DirManager* dm = static_cast<DirManager*>(addr);

    // first-time initialization
    dir_manager_init(dm);

    insert_node(dm, "/home");
    insert_node(dm, "/home/user");
    insert_node(dm, "/var");

    // flush + unmap (simulate shutdown)
    msync(addr, sizeof(DirManager), MS_SYNC);
    munmap(addr, sizeof(DirManager));
    close(fd);

    // remap (simulate restart)
    fd = open(PERSIST_FILE, O_RDWR, 0600);
    ASSERT_NE(fd, -1);

    addr = mmap(nullptr, sizeof(DirManager),
                PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_NE(addr, MAP_FAILED);

    DirManager* dm_after = static_cast<DirManager*>(addr);

    // IMPORTANT: reinitialize lock ONLY
    pthread_rwlock_init(&dm_after->rwlock, nullptr);

    // verify persistence
    EXPECT_NE(lookup_node(dm_after, "/home"), -1);
    EXPECT_NE(lookup_node(dm_after, "/home/user"), -1);
    EXPECT_NE(lookup_node(dm_after, "/var"), -1);

    munmap(addr, sizeof(DirManager));
    close(fd);
    unlink(PERSIST_FILE);
}

/* ---------------------------------------------------------
 * HashTable persistence
 * --------------------------------------------------------- */
TEST(PersistenceTest, HashTablePersistsAcrossRestart) {
    int fd;
    void* addr = map_file(PERSIST_FILE, sizeof(HashTable), fd);
    ASSERT_NE(addr, MAP_FAILED);

    HashTable* ht = static_cast<HashTable*>(addr);

    hash_init(ht);

    const char* key1 = "alpha";
    const char* key2 = "beta";

    uint64_t h1 = hash_path_poly(key1);
    uint64_t h2 = hash_path_poly(key2);

    ASSERT_TRUE(hash_insert(ht, h1, key1, 11));
    ASSERT_TRUE(hash_insert(ht, h2, key2, 22));

    // flush + unmap
    msync(addr, sizeof(HashTable), MS_SYNC);
    munmap(addr, sizeof(HashTable));
    close(fd);

    // remap
    fd = open(PERSIST_FILE, O_RDWR, 0600);
    ASSERT_NE(fd, -1);

    addr = mmap(nullptr, sizeof(HashTable),
                PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_NE(addr, MAP_FAILED);

    HashTable* ht_after = static_cast<HashTable*>(addr);

    // verify persistence
    EXPECT_EQ(hash_lookup(ht_after, h1, key1), 11);
    EXPECT_EQ(hash_lookup(ht_after, h2, key2), 22);

    munmap(addr, sizeof(HashTable));
    close(fd);
    unlink(PERSIST_FILE);
}
