cmake_minimum_required(VERSION 3.10)
project(FastDevFs LANGUAGES CXX)

# Use modern C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find FUSE3
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)

# Executable
add_executable(fastdevfs random.cpp)

# Include directories
target_include_directories(fastdevfs PRIVATE
    ${FUSE3_INCLUDE_DIRS}
)

# Compiler flags from pkg-config
target_compile_options(fastdevfs PRIVATE
    ${FUSE3_CFLAGS_OTHER}
)

# Link FUSE3
target_link_libraries(fastdevfs PRIVATE
    ${FUSE3_LIBRARIES}
)
