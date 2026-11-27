set(LIB_NAME lib_vga)
set(LIB_VERSION 0.1.0)
set(LIB_INCLUDES api)

set(LIB_DEPENDENT_MODULES "")

set(LIB_COMPILER_FLAGS -O3
                       -g)

XMOS_REGISTER_MODULE()
