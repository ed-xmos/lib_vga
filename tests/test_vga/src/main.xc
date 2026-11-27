// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <platform.h>

#ifndef TILE
#define TILE 0
#endif

extern void main_app(void);

int main(void) {
    par {
        on tile[TILE]: main_app();
    }
    return 0;
}
