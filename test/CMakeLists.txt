enable_testing()

find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/src/include)

add_executable(fdfs_tests
    test_dir_manager.cpp
    test_hash_store.cpp
    ../src/src/daemon/dir_manager.cpp
    ../src/src/daemon/hash_store.cpp
)

target_link_libraries(fdfs_tests
    gtest
    gtest_main
    pthread
)

add_test(NAME fdfs_tests COMMAND fdfs_tests)
