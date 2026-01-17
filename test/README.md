# FastDevFs Test Suite

## Overview

This directory contains comprehensive unit tests for the FastDevFs directory tree implementation using Google Test framework. The tests cover all major functionality including tree operations, hash map operations, thread safety, and edge cases.

## Test Files

### `test_adt.cpp`
Contains 20 comprehensive tests for Abstract Data Type (ADT) operations:

1. **InitializeSetsCorrectState** - Verifies tree initialization
2. **InsertRootNode** - Tests root node insertion
3. **InsertChildUnderParent** - Tests child insertion under parent
4. **InsertMultipleChildren** - Tests multiple children insertion
5. **HashindexReturnsCorrectIndex** - Verifies hash index lookup
6. **HashindexReturnsNegativeForNonExistent** - Tests non-existent file lookup
7. **DeleteLeafNode** - Tests leaf node deletion
8. **DeleteNodeWithChildrenRecursively** - Tests recursive deletion
9. **CannotDeleteRootNode** - Verifies root protection
10. **ChangeParent** - Tests parent change operation
11. **PreventDuplicateInsertions** - Verifies duplicate prevention
12. **NodeAllocationCounter** - Tests node counter accuracy
13. **EmptyFilenameHandling** - Tests input validation
14. **NonExistentParentDefaultsToRoot** - Tests default parent behavior
15. **ChangeParentPreventsCycles** - Verifies cycle prevention
16. **ThreadSafetyConcurrentInserts** - Tests concurrent insertions
17. **ThreadSafetyConcurrentDeleteAndInsert** - Tests mixed concurrent operations
18. **FreeListIntegrityAfterDeletions** - Verifies free list integrity
19. **ChangeParentToSameParentIsNoOp** - Tests no-op prevention
20. **DeepTreeDeletion** - Tests deep tree recursive deletion

### `test_hash.cpp`
Contains 14 comprehensive tests for hash map operations:

1. **CreateAndDestroy** - Tests hash map creation/destruction
2. **InsertAndGet** - Tests basic insert and get operations
3. **HasFunction** - Tests existence checking
4. **SetAndGet** - Tests set/get operations
5. **RemoveEntry** - Tests entry removal
6. **RemoveNonExistentKey** - Tests removal of non-existent keys
7. **UpdateExistingValue** - Tests value updates
8. **MultipleKeys** - Tests multiple key handling
9. **GetNonExistentReturnsDefault** - Tests default value behavior
10. **ClearByRemovingAll** - Tests complete clearing
11. **VariousStringLengths** - Tests different key lengths
12. **CollisionHandling** - Tests hash collision resolution
13. **OperatorBracketCreatesEntry** - Tests operator[] behavior
14. **SizeIncreasesCorrectly** - Tests size tracking

## Building Tests

### Prerequisites
```bash
# Install dependencies
sudo apt-get update
sudo apt-get install -y cmake build-essential libfuse3-dev
```

### Build Instructions

1. **Create build directory:**
   ```bash
   mkdir -p build
   cd build
   ```

2. **Configure with CMake:**
   ```bash
   cmake ..
   ```

3. **Build the project:**
   ```bash
   make
   ```

   This will:
   - Download Google Test automatically (via FetchContent)
   - Build the main FastDevFS executable
   - Build the `fastdevfs_adt` library
   - Build `test_adt` executable
   - Build `test_hash` executable

## Running Tests

### Run All Tests

From the build directory:
```bash
# Run all tests via CTest
ctest

# Run with verbose output
ctest --verbose

# Run specific test executable
./test_adt
./test_hash

# Run with Google Test flags
./test_adt --gtest_list_tests          # List all tests
./test_adt --gtest_filter=*Insert*     # Run only Insert tests
./test_adt --gtest_repeat=5            # Repeat tests 5 times
./test_adt --gtest_shuffle             # Shuffle test order
```

### Run Specific Test Cases

```bash
# Run only ADT tests
./test_adt

# Run only Hash tests
./test_hash

# Run specific test suite
./test_adt --gtest_filter=ADTTest.*

# Run specific test
./test_adt --gtest_filter=ADTTest.InsertRootNode
./test_hash --gtest_filter=HashMapTest.InsertAndGet
```

### Test Output Examples

#### Successful Test Run
```
[==========] Running 20 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 20 tests from ADTTest
[ RUN      ] ADTTest.InitializeSetsCorrectState
[       OK ] ADTTest.InitializeSetsCorrectState (0 ms)
...
[----------] 20 tests from ADTTest (15 ms total)
[==========] 20 tests from 1 test suite ran. (15 ms total)
[  PASSED  ] 20 tests.
```

#### Test Failure
```
[ RUN      ] ADTTest.InsertRootNode
test/test_adt.cpp:45: Failure
Expected: (rootIndex) >= (0), actual: -1 vs 0
[  FAILED  ] ADTTest.InsertRootNode (1 ms)
```

## Test Coverage

### ADT Tests Coverage

| Category | Tests | Status |
|----------|-------|--------|
| Initialization | 1 | ✅ |
| Insert Operations | 4 | ✅ |
| Lookup Operations | 2 | ✅ |
| Delete Operations | 3 | ✅ |
| Parent Operations | 4 | ✅ |
| Edge Cases | 3 | ✅ |
| Thread Safety | 2 | ✅ |
| Deep Operations | 1 | ✅ |

### Hash Tests Coverage

| Category | Tests | Status |
|----------|-------|--------|
| Basic Operations | 4 | ✅ |
| Remove Operations | 2 | ✅ |
| Update Operations | 1 | ✅ |
| Multiple Keys | 2 | ✅ |
| Edge Cases | 3 | ✅ |
| Collision Handling | 1 | ✅ |
| Size Tracking | 1 | ✅ |

## Test Categories

### Unit Tests
- Test individual functions in isolation
- Verify correct behavior with various inputs
- Check edge cases and error conditions

### Integration Tests
- Test interactions between components
- Verify correct data flow
- Check state consistency

### Thread Safety Tests
- Test concurrent operations
- Verify mutex protection
- Check for race conditions

### Stress Tests
- Test with large numbers of operations
- Verify performance under load
- Check memory management

## Troubleshooting

### Tests Fail to Build

**Issue**: CMake cannot find Google Test
```bash
# Solution: Update CMake and try again
# Google Test is automatically downloaded via FetchContent
cmake --version  # Should be >= 3.12
rm -rf build
mkdir build && cd build
cmake ..
make
```

**Issue**: Compilation errors
```bash
# Check C++ standard support
g++ --version  # Should support C++17

# Clean and rebuild
cd build
make clean
make
```

### Tests Fail at Runtime

**Issue**: Thread safety tests fail
- Check if system supports threading
- Verify mutex implementation
- Check for deadlocks

**Issue**: Memory-related failures
- Check for memory leaks with valgrind:
  ```bash
  valgrind --leak-check=full ./test_adt
  ```

### Debugging Failed Tests

1. **Run with verbose output:**
   ```bash
   ./test_adt --gtest_print_time=1
   ```

2. **Run single test:**
   ```bash
   ./test_adt --gtest_filter=ADTTest.InsertRootNode
   ```

3. **Use GDB:**
   ```bash
   gdb ./test_adt
   (gdb) run --gtest_filter=ADTTest.InsertRootNode
   ```

## Adding New Tests

### Adding ADT Tests

1. Open `test/test_adt.cpp`
2. Add new test in `ADTTest` class:
   ```cpp
   TEST_F(ADTTest, YourTestName) {
       // Your test code here
       EXPECT_EQ(actual, expected);
   }
   ```

3. Rebuild and run:
   ```bash
   cd build
   make test_adt
   ./test_adt --gtest_filter=ADTTest.YourTestName
   ```

### Adding Hash Tests

1. Open `test/test_hash.cpp`
2. Add new test in `HashMapTest` class:
   ```cpp
   TEST_F(HashMapTest, YourTestName) {
       // Your test code here
       EXPECT_TRUE(condition);
   }
   ```

## Continuous Integration

Tests can be integrated into CI/CD pipelines:

```yaml
# Example GitHub Actions
- name: Build and Test
  run: |
    mkdir build && cd build
    cmake ..
    make
    ctest --output-on-failure
```

## Performance Benchmarks

To measure test execution time:
```bash
time ./test_adt
time ./test_hash
```

## Test Maintenance

- **Update tests** when adding new features
- **Fix tests** when fixing bugs
- **Add tests** before fixing bugs (TDD approach)
- **Review test coverage** regularly

## Resources

- [Google Test Documentation](https://google.github.io/googletest/)
- [CMake Testing](https://cmake.org/cmake/help/latest/manual/ctest.1.html)
- FastDevFs Main README: `../README.md`

## License

Tests follow the same license as the main project. See `../LICENSE` for details.

