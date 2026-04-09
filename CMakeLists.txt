cmake_minimum_required(VERSION 3.12)

project(FastDevFS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
  cli11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.4.2
)
FetchContent_MakeAvailable(cli11)

add_library(fastdevfs_adt STATIC
    "src/daemon/directory tree/adt.cpp"
    "src/daemon/directory tree/hash.cpp"
    "src/daemon/fuse_lowlevel_ops.cpp"
)
target_include_directories(fastdevfs_adt PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${FUSE3_INCLUDE_DIRS}
)
target_compile_options(fastdevfs_adt PRIVATE ${FUSE3_CFLAGS_OTHER} -Wno-narrowing)

add_library(fastdevfs_common STATIC
    src/config.cpp
    src/ipc.cpp
    src/sha256.cpp
    src/ui.cpp
    src/dedup_server.cpp
    src/dedup_index.cpp
    src/library_manager.cpp
)
target_include_directories(fastdevfs_common PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(fastdevfs
    src/main.cpp
    src/library_dedup.cpp
)
target_include_directories(fastdevfs PRIVATE
    ${FUSE3_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(fastdevfs PRIVATE
    fastdevfs_adt
    fastdevfs_common
    ${FUSE3_LIBRARIES}
)
target_compile_options(fastdevfs PRIVATE ${FUSE3_CFLAGS_OTHER} -Wno-narrowing)

add_executable(fastdevfs-cli
    src/cli.cpp
)
target_include_directories(fastdevfs-cli PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(fastdevfs-cli PRIVATE
    fastdevfs_common
    CLI11::CLI11
)

add_library(library_predictor STATIC
    src/library_predictor.cpp
)
target_include_directories(library_predictor PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

enable_testing()

add_executable(test_predictor test/test_predictor.cpp)
target_link_libraries(test_predictor PRIVATE 
    library_predictor
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(test_predictor PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME PredictorTests COMMAND test_predictor)

add_executable(test_adt test/test_adt.cpp)
target_link_libraries(test_adt PRIVATE
    fastdevfs_adt
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(test_adt PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME AdtTests COMMAND test_adt)

add_executable(test_hash test/test_hash.cpp)
target_link_libraries(test_hash PRIVATE
    fastdevfs_adt
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(test_hash PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME HashTests COMMAND test_hash)

add_executable(test_persistence test/test_persistence.cpp)
target_link_libraries(test_persistence PRIVATE
    fastdevfs_adt
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(test_persistence PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME PersistenceTests COMMAND test_persistence)

add_executable(test_node_share test/test_node_share.cpp)
target_link_libraries(test_node_share PRIVATE
    fastdevfs_adt
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(test_node_share PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME NodeShareTests COMMAND test_node_share)