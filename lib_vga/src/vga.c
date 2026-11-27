// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <stdio.h>
#include <stdint.h>
#include <xcore/assert.h>
#include <xcore/clock.h>
#include <xcore/port.h>
#include "resource_adjust.h"
#include "vga.h"

// Define VGA_DBG_ACTIVE_PORT to a 1-bit port to output a strobe that is high
// during the active (frame buffer streaming) part of each frame.
#ifdef VGA_DBG_ACTIVE_PORT
static port_t p_dbg_active = VGA_DBG_ACTIVE_PORT;

static inline void vga_dbg_active(int value) {
  port_out(p_dbg_active, value);
}

static inline void vga_dbg_active_init(void) {
  port_enable(p_dbg_active);
  vga_dbg_active(0);
}
#else
static inline void vga_dbg_active(int value) { (void)value; }
static inline void vga_dbg_active_init(void) {}
#endif

typedef struct {
  int h_active;
  int h_front_active_ticks;
  int h_sync_ticks;
  int h_total_ticks;
} vga_horizontal_timing_t;

typedef struct {
  int v_repeat_count;
  int v_front;
  int v_active_rows_minus_1;
  int v_active_lines;
  int v_sync_minus_1;
  int v_sync_lines;
  int v_back;
  int v_total_lines;
} vga_vertical_timing_t;

__attribute__((always_inline)) 
static inline void vga_send_hsync(port_t p_hsync, port_timestamp_t line_start, const vga_horizontal_timing_t *timing) {
  const port_timestamp_t fall = line_start + timing->h_front_active_ticks;
  port_set_trigger_time(p_hsync, fall);
  port_out(p_hsync, 0);
  // Blocks until the fall is output; harmless as the RGB port is idle until the next line's timed start
  // We have VGA_H_FRONT + most of VGA_H_SYNC lots of 40ns clock cycles of idle time here to do things if needed!
  port_set_trigger_time(p_hsync, fall + timing->h_sync_ticks);
  port_out(p_hsync, 1);
}

__attribute__((always_inline)) 
static inline void vga_send_frame_line(port_t p_rgb, const uint32_t *frame_ptr, port_t p_hsync, port_timestamp_t line_start, const vga_horizontal_timing_t *timing) {
  port_out_at_time(p_rgb, line_start, frame_ptr[0]);
  for(int x = 1; x < timing->h_active; x++) {
    port_out(p_rgb, frame_ptr[x]);
  }
  // The pins hold the last value output, so this keeps them black for the whole blanking period
  port_out(p_rgb, 0x00000000);
  vga_send_hsync(p_hsync, line_start, timing);
}

void vga_controller(vga_config_t *config) {
  printf("VGA Controller Started\n");

  vga_dbg_active_init();

  const uint32_t black = 0x00000000;

  clock_enable(config->clk_bit);

  port_enable(config->p_hsync);
  VGA_set_pad_drive_strength(config->p_hsync, VGA_DRIVE_4MA);
  port_out(config->p_hsync, 1);
  port_set_clock(config->p_hsync, config->clk_bit);


  port_enable(config->p_vsync);
  VGA_set_pad_drive_strength(config->p_vsync, VGA_DRIVE_4MA);
  port_out(config->p_vsync, 1);
  port_set_clock(config->p_vsync, config->clk_bit);
  
  port_start_buffered (config->p_rgb, 32);
  VGA_set_pad_drive_strength(config->p_rgb, VGA_DRIVE_12MA);
  port_out(config->p_rgb, black);
  port_sync(config->p_rgb);
  port_set_clock(config->p_rgb, config->clk_bit);
  


  const int port_width_bits = vga_port_width_bits(config->p_rgb);
  xassert(port_width_bits == 4 || port_width_bits == 8 || port_width_bits == 16);
  const int pixels_per_word = vga_pixels_per_word(port_width_bits);
  const int port_cycles_per_word = (sizeof(uint32_t) * 8) / port_width_bits;
  const int output_scale = pixels_per_word / vga_pixels_per_word(16);

  clock_set_source_clk_ref(config->clk_bit);
  int pixel_clock_divider = config->resolution == VGA_640_480 ? 4 : 8;
  clock_set_divide(config->clk_bit, (pixel_clock_divider * output_scale / port_cycles_per_word));
  int num_h_pixels_divider = config->resolution == VGA_640_480 ? 2 : 4;
  const int horizontal_output_divider = num_h_pixels_divider * output_scale;
  const vga_horizontal_timing_t horizontal_timing = {
    .h_active = VGA_H_ACTIVE / horizontal_output_divider,
    .h_front_active_ticks = (port_cycles_per_word * (VGA_H_FRONT + VGA_H_ACTIVE)) / horizontal_output_divider,
    .h_sync_ticks = (port_cycles_per_word * VGA_H_SYNC) / horizontal_output_divider,
    .h_total_ticks = (port_cycles_per_word * VGA_H_TOTAL) / horizontal_output_divider,
  };
  const int vertical_repeat_count = config->resolution == VGA_640_480 ? 1 : 2;
  const int vertical_active_rows = VGA_V_ACTIVE / vertical_repeat_count;
  const vga_vertical_timing_t vertical_timing = {
    .v_repeat_count = vertical_repeat_count,
    .v_front = VGA_V_FRONT,
    .v_active_lines = vertical_active_rows * vertical_repeat_count,
    .v_active_rows_minus_1 = vertical_active_rows - 1,
    .v_sync_minus_1 = VGA_V_SYNC - 1,
    .v_sync_lines = VGA_V_SYNC,
    .v_back = VGA_V_BACK,
    .v_total_lines = VGA_V_FRONT + (vertical_active_rows * vertical_repeat_count) + VGA_V_SYNC + VGA_V_BACK,
  };


  clock_stop(config->clk_bit);
  clock_start(config->clk_bit); // Clear port timers connected to the clock
  clock_stop(config->clk_bit);

  // https://projectf.io/posts/video-timings-vga-720p-1080p/#640x480-60-hz

  /*
      Standard 640 x 480 VGA timing

                      800 pixel clocks
       <---------------------------------------->

       +----------------------+------+-----+------+
       |                      |      |     |      |
       |                      |      |     |      |
       |                      |      |     |      |
       |                      |      |     |      |
       |                      |      |     |      |
       |                      |      |     |      |
       +----------------------+------+-----+------+
       ^                      ^      ^     ^      ^
       |                      |      |     |      |
      Visible               Front  HSYNC  Back   Next
      video                 porch         porch  line

                640             16     96    48
  */

  // VSYNC edges are scheduled up to max(V_FRONT, V_SYNC) + 1 lines ahead and must stay within the 16-bit port timer
  xassert(horizontal_timing.h_active >= 1);
  xassert(((VGA_V_FRONT > VGA_V_SYNC ? VGA_V_FRONT : VGA_V_SYNC) + 1) * horizontal_timing.h_total_ticks < 0x10000);

  // Leave a line for the thread to schedule the first timed output after the clock starts
  port_timestamp_t line_start = horizontal_timing.h_total_ticks;
  const int frame_words_per_line = vga_words_per_line(VGA_H_ACTIVE, port_width_bits);
  
  // Wait for the client to provide the frame buffer
  while(config->frame_ptr_base == NULL) {
  }

  xassert(((uintptr_t)config->frame_ptr_base % 4) == 0);
  const uint32_t *frame_ptr = config->frame_ptr_base;

  clock_start(config->clk_bit);

  while(1) {
    vga_dbg_active(1);

    for(int y = 0; y <= vertical_timing.v_active_rows_minus_1; y++) {
      for(int repeat = 0; repeat < vertical_timing.v_repeat_count; repeat++) {
        vga_send_frame_line(config->p_rgb, frame_ptr, config->p_hsync, line_start, &horizontal_timing);
        line_start += horizontal_timing.h_total_ticks;
      }
      frame_ptr += frame_words_per_line;
    }

    vga_dbg_active(0);

    port_set_trigger_time(config->p_vsync, line_start + vertical_timing.v_front * horizontal_timing.h_total_ticks);
    port_out(config->p_vsync, 0);

    for(int y = 0; y < vertical_timing.v_front; y++) {
      vga_send_hsync(config->p_hsync, line_start, &horizontal_timing);
      line_start += horizontal_timing.h_total_ticks;
    }

    // First sync line: once its hsync is issued the VSYNC fall has been output
    vga_send_hsync(config->p_hsync, line_start, &horizontal_timing);
    line_start += horizontal_timing.h_total_ticks;
    port_set_trigger_time(config->p_vsync, line_start + vertical_timing.v_sync_minus_1 * horizontal_timing.h_total_ticks);
    port_out(config->p_vsync, 1);

    for(int y = 0; y < vertical_timing.v_sync_minus_1 + vertical_timing.v_back; y++) {
      vga_send_hsync(config->p_hsync, line_start, &horizontal_timing);
      line_start += horizontal_timing.h_total_ticks;
    }
    frame_ptr = config->frame_ptr_base;
  }
}
