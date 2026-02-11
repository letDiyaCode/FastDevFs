cmake_minimum_required(VERSION 3.12)

project(FastDevFS)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)


add_executable(FastDevFS 
    main.cpp

    src/src/fuse_functions/getattr.cpp
    src/src/fuse_functions/readdir.cpp
    src/src/fuse_functions/opendir.cpp
    src/src/fuse_functions/mkdir.cpp
    src/src/fuse_functions/rmdir.cpp
    src/src/fuse_functions/access.cpp
    src/src/fuse_functions/statfs.cpp
    src/src/fuse_functions/file_funcs.cpp
    src/src/fuse_functions/utimens.cpp

    src/src/daemon/dir_manager.cpp
    src/src/daemon/hash.cpp
    src/src/daemon/file_io.cpp
    src/src/config.cpp
)

target_include_directories(FastDevFS PRIVATE 
    ${FUSE3_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(FastDevFS PRIVATE ${FUSE3_LIBRARIES})
target_compile_options(FastDevFS PRIVATE ${FUSE3_CFLAGS_OTHER})

enable_testing()
add_subdirectory(test)