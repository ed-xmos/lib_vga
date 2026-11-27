cmake_minimum_required(VERSION 3.21)
include($ENV{XMOS_CMAKE_PATH}/xcommon.cmake)
project(app_vga)

if(NOT DEFINED APP_HW_TARGET AND NOT BUILD_NATIVE)
    set(APP_HW_TARGET XK-EVK-XU316)
endif()

set(APP_INCLUDES            src)

# Ports chosen to suit the XK-EVK-XU316 expansion headers; override to match
# the wiring of the VGA resistor DAC on other boards.
set(APP_COMPILER_FLAGS      -O3
                            -g
                            -report
                            -Wall
                            -DVGA_RESOLUTION=VGA_320_240
                            -DVGA_RGB_PORT=XS1_PORT_4B
                            -DVGA_HSYNCH_PORT=XS1_PORT_1M
                            -DVGA_VSYNCH_PORT=XS1_PORT_1O
                            -DTILE=1)

set(APP_DEPENDENT_MODULES   "lib_vga")

set(XMOS_SANDBOX_DIR ${CMAKE_CURRENT_LIST_DIR}/../../..)

XMOS_REGISTER_APP()
