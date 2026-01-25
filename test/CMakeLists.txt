cmake_minimum_required(VERSION 3.10)

find_package(GTest REQUIRED)

add_executable(run_tests
    test_dir_manager.cpp
    test_hash_store.cpp
    ../src/src/daemon/dir_manager.cpp
    ../src/src/daemon/hash.cpp
)

target_include_directories(run_tests PRIVATE
    ../src/include
)

target_link_libraries(run_tests
    GTest::GTest
    GTest::Main
    pthread
)
