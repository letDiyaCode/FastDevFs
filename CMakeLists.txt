cmake_minimum_required(VERSION 3.12)

project(FastDevFS LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)

# Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Main executable
add_executable(fastdevfs src/main.cpp)
target_include_directories(fastdevfs PRIVATE
    ${FUSE3_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(fastdevfs PRIVATE
    fastdevfs_adt
    ${FUSE3_LIBRARIES}
)
target_compile_options(fastdevfs PRIVATE ${FUSE3_CFLAGS_OTHER} -Wno-narrowing)

# Library for ADT, Hash, and FUSE functions
add_library(fastdevfs_adt STATIC
    "src/daemon/directory tree/adt.cpp"
    "src/daemon/directory tree/hash.cpp"
    "src/fuse functions/fuse_ops_init.cpp"
    "src/fuse functions/fs_access.cpp"
    "src/fuse functions/fs_getattr.cpp"
    "src/fuse functions/fs_mkdir.cpp"
    "src/fuse functions/fs_opendir.cpp"
    "src/fuse functions/fs_readdir.cpp"
    "src/fuse functions/fs_rmdir.cpp"
    "src/fuse functions/fs_statfs.cpp"
    "src/fuse functions/fs_utimens.cpp"
    "src/fuse functions/fs_open.cpp"
    "src/fuse functions/fs_read.cpp"
    "src/fuse functions/fs_write.cpp"
    "src/fuse functions/fs_create.cpp"
    "src/fuse functions/fs_truncate.cpp"
    "src/fuse functions/fs_unlink.cpp"
)
target_include_directories(fastdevfs_adt PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${FUSE3_INCLUDE_DIRS}
)
target_compile_options(fastdevfs_adt PRIVATE ${FUSE3_CFLAGS_OTHER} -Wno-narrowing)

# Test executable for ADT
add_executable(test_adt test/test_adt.cpp)
target_link_libraries(test_adt PRIVATE 
    fastdevfs_adt
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(test_adt PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
)

# Test executable for Hash
add_executable(test_hash test/test_hash.cpp)
target_link_libraries(test_hash PRIVATE 
    fastdevfs_adt
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(test_hash PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
)

# Test executable for Persistence


# Minimal test executable




# Enable testing
enable_testing()

# Test executable for fs_open
# add_executable(test_open test/test_open.cpp 
#     "src/fuse functions/fs_open.cpp"
#     "src/daemon/directory tree/adt.cpp"
#     "src/daemon/directory tree/hash.cpp"
# )
# target_link_libraries(test_open PRIVATE 
#     ${FUSE3_LIBRARIES}
# )
# target_include_directories(test_open PRIVATE 
#     ${CMAKE_SOURCE_DIR}/include
#     ${FUSE3_INCLUDE_DIRS}
# )
# target_compile_options(test_open PRIVATE ${FUSE3_CFLAGS_OTHER})

# Test executable for FUSE operations
add_executable(test_fuse_ops test/test_fuse_ops.cpp)
target_link_libraries(test_fuse_ops PRIVATE 
    fastdevfs_adt
    GTest::gtest
    GTest::gtest_main
    ${FUSE3_LIBRARIES}
)
target_include_directories(test_fuse_ops PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${FUSE3_INCLUDE_DIRS}
)
target_compile_options(test_fuse_ops PRIVATE ${FUSE3_CFLAGS_OTHER} -Wno-narrowing)

add_test(NAME AdtTests COMMAND test_adt)
add_test(NAME HashTests COMMAND test_hash)
add_test(NAME FuseOpsTests COMMAND test_fuse_ops)