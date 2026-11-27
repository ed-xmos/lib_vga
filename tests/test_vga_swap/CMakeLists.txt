cmake_minimum_required(VERSION 3.21)
include($ENV{XMOS_CMAKE_PATH}/xcommon.cmake)

project(test_vga_swap)

if(NOT DEFINED APP_HW_TARGET AND NOT BUILD_NATIVE)
    set(APP_HW_TARGET XK-EVK-XU316)
endif()

set(APP_INCLUDES src)

set(APP_COMMON_BUILD_FLAGS
    -O3
    -g
    -report
    -Wall
)

# Shortened frame so a simulation covers several whole frames quickly.
# test_vga.py reads these -D flags to configure the test bench (vga_tb.py).
set(TEST_TIMING_FLAGS
    -DVGA_H_ACTIVE=64
    -DVGA_H_FRONT=8
    -DVGA_H_SYNC=8
    -DVGA_H_BACK=8
    -DVGA_V_ACTIVE=32
    -DVGA_V_FRONT=8
    -DVGA_V_SYNC=8
    -DVGA_V_BACK=8
)

set(APP_COMPILER_FLAGS_640_8bpp ${APP_COMMON_BUILD_FLAGS} ${TEST_TIMING_FLAGS} -DVGA_RESOLUTION=VGA_640_480 -DVGA_RGB_PORT=XS1_PORT_8A)

set(APP_DEPENDENT_MODULES "lib_vga")

set(XMOS_SANDBOX_DIR ${CMAKE_CURRENT_LIST_DIR}/../../..)

XMOS_REGISTER_APP()
