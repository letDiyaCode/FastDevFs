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
add_executable(FastDevFS main.cpp)
target_include_directories(FastDevFS PRIVATE 
    ${FUSE3_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(FastDevFS PRIVATE ${FUSE3_LIBRARIES})
target_compile_options(FastDevFS PRIVATE ${FUSE3_CFLAGS_OTHER})

# Library for ADT and Hash functions
add_library(fastdevfs_adt STATIC
    "src/daemon/directory tree/adt.cpp"
    "src/daemon/directory tree/hash.cpp"
)
target_include_directories(fastdevfs_adt PUBLIC 
    ${CMAKE_SOURCE_DIR}/include
)

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

# Enable testing
enable_testing()
add_test(NAME ADTTests COMMAND test_adt)
add_test(NAME HashTests COMMAND test_hash)