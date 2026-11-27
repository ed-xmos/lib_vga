// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <xcore/assert.h>
#include <xcore/parallel.h>
#include "vga.h"

// Overridden per build config in CMakeLists.txt
#ifndef VGA_RESOLUTION
#define VGA_RESOLUTION VGA_640_480
#endif
#ifndef VGA_RGB_PORT
#define VGA_RGB_PORT XS1_PORT_8A
#endif
#ifndef VGA_HSYNCH_PORT
#define VGA_HSYNCH_PORT XS1_PORT_1A
#endif
#ifndef VGA_VSYNCH_PORT
#define VGA_VSYNCH_PORT XS1_PORT_1B
#endif

DECLARE_JOB(client, (vga_config_t *));

#ifdef VGA_STRESS
// Busy work loop so the VGA controller only gets a 1/VGA_STRESS share of the
// processor while streaming pixels (VGA_STRESS = total active thread count)
static void burn_forever(void) {
  volatile uint32_t counter = 0;
  while(1) {
    counter++;
  }
}

DECLARE_JOB(burn, (void));
void burn(void) {
  burn_forever();
}
#endif

// Fills the frame buffer with an incrementing word pattern that vga_tb.py
// checks pixel by pixel in the simulation trace.
void client(vga_config_t *config) {
  printf("Client start\n");
  const size_t frame_word_count =
      VGA_FRAME_BUFF_WORDS(config->resolution, vga_port_width_bits(config->p_rgb));
  uint32_t *frame_buff = malloc(VGA_FRAME_BUFF_SIZE(config->resolution, vga_port_width_bits(config->p_rgb)));
  xassert(frame_buff != NULL);
  for(size_t i = 0; i < frame_word_count; i++) {
    frame_buff[i] = 0x55550000 + i;
  }
  config->frame_ptr_base = frame_buff;
  printf("Sent frame buffer\n");
#ifdef VGA_STRESS
  burn_forever(); // Keep this thread busy too so 8 threads stay active
#endif
}

static void print_config(vga_config_t *config) {
  printf("VGA Configuration:\n");
  printf("  Resolution: %s\n", config->resolution == VGA_640_480 ? "640x480" : "320x240");
  printf("  Color Depth: %d\n", vga_port_width_bits(config->p_rgb));
  printf("  HSYNC Port: 0x%x\n", config->p_hsync);
  printf("  VSYNC Port: 0x%x\n", config->p_vsync);
  printf("  RGB Port: 0x%x\n", config->p_rgb);
  printf("  Clock Bit: 0x%x\n", config->clk_bit);
  printf("  Frame Pointer Base: %p\n", config->frame_ptr_base);
}

void main_app(void) {
  vga_config_t config =
  {
    .resolution = VGA_RESOLUTION,
    .p_hsync = VGA_HSYNCH_PORT,
    .p_vsync = VGA_VSYNCH_PORT,
    .p_rgb = VGA_RGB_PORT,
    .clk_bit = XS1_CLKBLK_1,
    .frame_ptr_base = NULL
  };

  print_config(&config);

#if defined(VGA_STRESS) && VGA_STRESS == 8
  // 8 threads in total: the controller runs at the minimum MIPS share (75 MHz)
  PAR_JOBS(
    PJOB(vga_controller, (&config)),
    PJOB(client, (&config)),
    PJOB(burn, ()),
    PJOB(burn, ()),
    PJOB(burn, ()),
    PJOB(burn, ()),
    PJOB(burn, ()),
    PJOB(burn, ())
  );
#elif defined(VGA_STRESS) && VGA_STRESS == 5
  // 5 threads in total: the controller runs at the full thread speed (120 MHz)
  // but with every pipeline slot contended
  PAR_JOBS(
    PJOB(vga_controller, (&config)),
    PJOB(client, (&config)),
    PJOB(burn, ()),
    PJOB(burn, ()),
    PJOB(burn, ())
  );
#else
  PAR_JOBS(
    PJOB(vga_controller, (&config)),
    PJOB(client, (&config))
  );
#endif
}
