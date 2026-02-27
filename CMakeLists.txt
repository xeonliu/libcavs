cmake_minimum_required(VERSION 3.10)
project(libcavs)

set(CMAKE_C_STANDARD 99)

# Create the library
add_library(cavs libcavs.c)

# Link pthread (required for libcavs threading)
find_package(Threads REQUIRED)
target_link_libraries(cavs Threads::Threads)

# Create the executable
add_executable(cavs_decode main.c)
target_link_libraries(cavs_decode cavs)
