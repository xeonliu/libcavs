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

# ---- MD5 regression test ----
enable_testing()

add_executable(test_md5 test_md5.c)
target_link_libraries(test_md5 cavs)

# On macOS CommonCrypto is built-in; on Linux link OpenSSL
if(NOT APPLE)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(test_md5 OpenSSL::Crypto)
endif()

add_test(
    NAME yuv_md5_match
    COMMAND test_md5 ${CMAKE_SOURCE_DIR}/sample/CCTV-9.avs
)
