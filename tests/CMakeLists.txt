cmake_minimum_required(VERSION 3.21)
include($ENV{XMOS_CMAKE_PATH}/xcommon.cmake)
project(tests)

add_subdirectory(test_vga)
add_subdirectory(test_vga_swap)
