// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#pragma once

#include <stdint.h>
#include <xcore/clock.h>
#include <xcore/parallel.h>
#include <xcore/port.h>

/** Output resolution of the VGA controller.
 *
 * Both resolutions use industry standard 640x480 @ 60 Hz signal timing;
 * ``VGA_320_240`` outputs each frame buffer pixel twice horizontally and
 * each line twice vertically.
 */
typedef enum{
  VGA_320_240,
  VGA_640_480
} vga_resolution_t;

/** Configuration and run-time state of a VGA controller instance.
 *
 * All fields except ``frame_ptr_base`` must be set before calling
 * vga_controller(). Set ``frame_ptr_base`` (from another thread) to a
 * word-aligned frame buffer to start video output.
 */
typedef struct {
  vga_resolution_t resolution; /**< Output resolution. */
  port_t p_hsync;              /**< 1-bit horizontal sync port. */
  port_t p_vsync;              /**< 1-bit vertical sync port. */
  port_t p_rgb;                /**< RGB pixel data port. The port width (4, 8 or 16) sets the bits per pixel. */
  xclock_t clk_bit;            /**< Clock block used to generate the pixel clock. */
  uint32_t * volatile frame_ptr_base; /**< Frame buffer to display; must be 32-bit word aligned. Initialise to NULL; the
                                           controller waits until it is set. Re-read once per frame during vertical blanking,
                                           so it may be updated at any time to swap buffers without tearing. */
} vga_config_t;

#ifndef VGA_H_ACTIVE
/** Active horizontal pixels (default: 640).
 *
 * This and the other horizontal and vertical timing values may be overridden
 * on the compiler command line, e.g. to shorten frames during simulation.
 */
#define VGA_H_ACTIVE  640
#endif
#ifndef VGA_H_FRONT
/** Horizontal front porch in pixels (default: 16). */
#define VGA_H_FRONT    16
#endif
#ifndef VGA_H_SYNC
/** Horizontal sync pulse width in pixels (default: 96). */
#define VGA_H_SYNC     96
#endif
#ifndef VGA_H_BACK
/** Horizontal back porch in pixels (default: 48). */
#define VGA_H_BACK     48
#endif

/** Total pixels per horizontal line, including blanking. */
#define VGA_H_TOTAL   (VGA_H_ACTIVE + VGA_H_FRONT + VGA_H_SYNC + VGA_H_BACK)

#ifndef VGA_V_ACTIVE
/** Active vertical lines (default: 480). */
#define VGA_V_ACTIVE  480
#endif
#ifndef VGA_V_FRONT
/** Vertical front porch in lines (default: 10). */
#define VGA_V_FRONT    10
#endif
#ifndef VGA_V_SYNC
/** Vertical sync pulse width in lines (default: 2). */
#define VGA_V_SYNC      2
#endif
#ifndef VGA_V_BACK
/** Vertical back porch in lines (default: 33). */
#define VGA_V_BACK     33
#endif

/** Total lines per frame, including blanking. */
#define VGA_V_TOTAL   (VGA_V_ACTIVE + VGA_V_FRONT + VGA_V_SYNC + VGA_V_BACK)

/** Frame buffer size in 32-bit words for a resolution and colour depth.
 *
 * The line stride is always a full ``VGA_H_ACTIVE`` pixels; at ``VGA_320_240``
 * only the first 320 pixels of each of the first 240 lines are displayed, so
 * half the number of lines is required. Both arguments may be run-time values,
 * or constants to give a constant expression for static allocation, e.g.:
 *
 * \code
 * uint32_t frame_buff[VGA_FRAME_BUFF_WORDS(VGA_320_240, 8)];
 * \endcode
 *
 * \param resolution      A ::vga_resolution_t value.
 * \param bits_per_pixel  Colour depth: the width of the RGB port (4, 8 or 16).
 */
#define VGA_FRAME_BUFF_WORDS(resolution, bits_per_pixel) \
  (((VGA_H_ACTIVE * (bits_per_pixel) + 31) / 32) * \
   ((resolution) == VGA_640_480 ? VGA_V_ACTIVE : VGA_V_ACTIVE / 2))

/** Frame buffer size in bytes for a resolution and colour depth.
 *
 * See VGA_FRAME_BUFF_WORDS() for details of the arguments and buffer layout.
 */
#define VGA_FRAME_BUFF_SIZE(resolution, bits_per_pixel) \
  (VGA_FRAME_BUFF_WORDS(resolution, bits_per_pixel) * sizeof(uint32_t))

/** Number of pixels packed into each 32-bit frame buffer word. */
static inline int vga_pixels_per_word(int bits_per_pixel) {
  return (sizeof(uint32_t) * 8) / bits_per_pixel;
}

/** Number of 32-bit words needed to hold one line of ``pixel_count`` pixels. */
static inline int vga_words_per_line(int pixel_count, int bits_per_pixel) {
  const int pixels_per_word = vga_pixels_per_word(bits_per_pixel);
  return (pixel_count + pixels_per_word - 1) / pixels_per_word;
}

/** Width in bits of a port, e.g. 8 for XS1_PORT_8A. */
static inline int vga_port_width_bits(port_t p) {
  return ((uint32_t)p >> 16) & 0xff;
}

/** VGA controller thread.
 *
 * Drives hsync, vsync and RGB pixel data with 640x480 @ 60 Hz timing from a
 * single thread. The width of ``config->p_rgb`` selects the pixel format:
 * 4 bpp (IRGB), 8 bpp (RGB332) or 16 bpp (RGB565).
 *
 * Runs forever; the screen stays black until ``config->frame_ptr_base`` is
 * set to a frame buffer.
 *
 * \param config  Controller configuration; must remain valid forever.
 */
DECLARE_JOB(vga_controller, (vga_config_t *));
