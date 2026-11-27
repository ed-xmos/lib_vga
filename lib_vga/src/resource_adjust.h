// Copyright 2023 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

// Macro to adjust input pad sample timing for the round trip delay. Supports 0 (default) to 5 core clock cycles.
// Larger numbers increase hold time but reduce setup time.
#define VGA_PORT_DELAY      0x7007
#define VGA_DELAY_SHIFT    0x3
#define VGA_set_pad_sample_delay(port, delay)  {__asm__ __volatile__ ("setc res[%0], %1": : "r" (port) , "r" ((delay << VGA_DELAY_SHIFT) | VGA_PORT_DELAY));}

// Macro to adjust input pad capture clock edge
#define VGA_PORT_SAMPLE     0x4007
#define VGA_SAMPLE_SHIFT   0x3
#define VGA_SAMPLE_RISING   0x0 // Default for input port. Reduces setup time, increases hold time.
#define VGA_SAMPLE_FALLING  0x1 // Increases setup time, reduces hold time
#define VGA_set_pad_sample_edge(port, edge)  {__asm__ __volatile__ ("setc res[%0], %1": : "r" (port) , "r" ((edge << VGA_SAMPLE_SHIFT) | VGA_PORT_SAMPLE));}


// Pad control defines
#define VGA_PAD_CONTROL     0x0006
#define VGA_DRIVE_2MA       0x0
#define VGA_DRIVE_4MA       0x1
#define VGA_DRIVE_8MA       0x2
#define VGA_DRIVE_12MA      0x3
#define VGA_DRIVE_SHIFT     20
#define VGA_ENABLE_SSLEW    (1 << 22)
#define VGA_ENABLE_SCHMITT  (1 << 23)
#define VGA_PAD_DRIVE_MODE  0x0003
#define VGA_DRIVE_BOTH      0x0
#define VGA_DRIVE_HIGH      0x1
#define VGA_DRIVE_LOW       0x2
#define VGA_MODE_SHIFT     0

// Macro to adjust the pad output drive strength
#define VGA_set_pad_drive_strength(port, strength)  {__asm__ __volatile__ ("setc res[%0], %1": : "r" (port) , "r" ((strength << VGA_DRIVE_SHIFT) | VGA_PAD_CONTROL));}
// Macro to enable the schmitt input
#define VGA_set_pad_input_schmitt(port)  {__asm__ __volatile__ ("setc res[%0], %1": : "r" (port) , "r" (VGA_ENABLE_SCHMITT | VGA_PAD_CONTROL));}
// Macro to enable the drive mode (open drain/source or complementary)
#define VGA_set_pad_drive_mode(port, mode)  {__asm__ __volatile__ ("setc res[%0], %1": : "r" (port) , "r" ((mode << VGA_MODE_SHIFT) | VGA_PAD_DRIVE_MODE));}

// Macro to adjust the clock block rise or fall delays up to 512 core clocks
#define VGA_set_clock_rise_delay_asm(clk, delay)  {__asm__ __volatile__ ("setc res[%0], %1": : "r" (clk) , "r" ((delay << 3) | 0x9007));}
#define VGA_set_clock_fall_delay_asm(clk, delay)  {__asm__ __volatile__ ("setc res[%0], %1": : "r" (clk) , "r" ((delay << 3) | 0x8007));}