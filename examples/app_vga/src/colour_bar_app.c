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

// 4bpp is IRGB (I = bit 3, B = bit 0); 8bpp is RGB332 and 16bpp is RGB565, R in the top bits
static uint32_t vga_rgb(uint8_t r, uint8_t g, uint8_t b, int bpp) {
  switch(bpp) {
    case 16:
      return ((uint32_t)(r >> 3) << 11) | ((uint32_t)(g >> 2) << 5) | (b >> 3);
    case 8:
      return ((uint32_t)(r >> 5) << 5) | ((uint32_t)(g >> 5) << 2) | (b >> 6);
    case 4: {
      // Quantise to CGA levels 0, 0x55, 0xAA, 0xFF: colour bit is the high level bit, intensity the low
      const int lr = r >> 6, lg = g >> 6, lb = b >> 6;
      int max = lr > lg ? lr : lg;
      max = max > lb ? max : lb;
      return ((uint32_t)(max & 1) << 3) | ((uint32_t)(lr >> 1) << 2) | ((uint32_t)(lg >> 1) << 1) | (lb >> 1);
    }
    default:
      xassert(0);
      return 0;
  }
}

DECLARE_JOB(client_colour_bars, (vga_config_t *));

// Colour bars over the top 3/4 of the screen, greyscale ramp below
void client_colour_bars(vga_config_t *config) {
  static const uint8_t bars[8][3] = {
    {0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0x00}, {0x00, 0xFF, 0xFF}, {0x00, 0xFF, 0x00},
    {0xFF, 0x00, 0xFF}, {0xFF, 0x00, 0x00}, {0x00, 0x00, 0xFF}, {0x00, 0x00, 0x00},
  };
  printf("Colour bars client start\n");
  const int bpp = vga_port_width_bits(config->p_rgb);
  const int pixels_per_word = vga_pixels_per_word(bpp);
  const int frame_words_per_line = vga_words_per_line(VGA_H_ACTIVE, bpp);
  const size_t frame_word_count = VGA_FRAME_BUFF_WORDS(config->resolution, bpp);
  uint32_t *frame_buff = calloc(frame_word_count, sizeof(*frame_buff));
  xassert(frame_buff != NULL);

  // At 320x240 the controller only shows the top-left quarter of the buffer
  const int scale = config->resolution == VGA_640_480 ? 1 : 2;
  const int width = VGA_H_ACTIVE / scale;
  const int height = VGA_V_ACTIVE / scale;
  const int ramp_steps = bpp == 4 ? 4 : 16;

  for(int y = 0; y < height; y++) {
    uint32_t *line = frame_buff + (size_t)y * frame_words_per_line;
    for(int x = 0; x < width; x++) {
      uint32_t pixel;
      if(y < (height * 3) / 4) {
        const uint8_t *c = bars[(x * 8) / width];
        pixel = vga_rgb(c[0], c[1], c[2], bpp);
      } else {
        const int step = (x * ramp_steps) / width;
        const uint8_t level = (uint8_t)((step * 0xFF) / (ramp_steps - 1));
        pixel = vga_rgb(level, level, level, bpp);
      }
      line[x / pixels_per_word] |= pixel << ((x % pixels_per_word) * bpp);
    }
  }
  config->frame_ptr_base = frame_buff;
  printf("Sent colour bars frame buffer\n");
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

  PAR_JOBS(
    PJOB(vga_controller, (&config)),
    PJOB(client_colour_bars, (&config))
  );
}
