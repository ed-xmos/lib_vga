// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <xcore/assert.h>
#include <xcore/hwtimer.h>
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

DECLARE_JOB(client_swap, (vga_config_t *));

// Double buffering test client: swaps frame_ptr_base between two buffers,
// each filled with a distinct incrementing pattern. vga_tb.py verifies that
// every displayed frame comes entirely from one buffer (no tearing) and that
// both buffers are displayed over the course of the trace.
void client_swap(vga_config_t *config) {
  printf("Swap client start\n");
  const int bpp = vga_port_width_bits(config->p_rgb);
  const size_t frame_word_count = VGA_FRAME_BUFF_WORDS(config->resolution, bpp);
  uint32_t *buff_a = malloc(VGA_FRAME_BUFF_SIZE(config->resolution, bpp));
  uint32_t *buff_b = malloc(VGA_FRAME_BUFF_SIZE(config->resolution, bpp));
  xassert(buff_a != NULL && buff_b != NULL);
  for(size_t i = 0; i < frame_word_count; i++) {
    buff_a[i] = 0x55550000 + i;
  }
  for(size_t i = 0; i < frame_word_count; i++) {
    buff_b[i] = 0xAAAA0000 + i;
  }

  // One frame period plus one line, in 100 MHz timer ticks (4 per 25 MHz
  // pixel): the swap point walks through the frame, including across the
  // point where the controller latches the pointer
  const uint32_t swap_period_ticks = (VGA_H_TOTAL * VGA_V_TOTAL + VGA_H_TOTAL) * 4;

  uint32_t *buffs[2] = {buff_a, buff_b};
  config->frame_ptr_base = buffs[0];
  printf("Sent first frame buffer\n");

  hwtimer_t tmr = hwtimer_alloc();
  uint32_t t = hwtimer_get_time(tmr);
  for(unsigned n = 1;; n++) {
    t += swap_period_ticks;
    hwtimer_wait_until(tmr, t);
    config->frame_ptr_base = buffs[n % 2];
  }
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

  PAR_JOBS(
    PJOB(vga_controller, (&config)),
    PJOB(client_swap, (&config))
  );
}
