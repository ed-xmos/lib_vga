:orphan:

###############################
lib_vga: VGA display controller
###############################

:vendor: XMOS
:version: 0.1.0
:scope: General Use
:description: Software VGA display controller for xcore.ai
:category: Graphics & Display
:keywords: VGA, video, display, graphics
:devices: xcore.ai

*******
Summary
*******

``lib_vga`` is a software VGA display controller for xcore.ai devices. A single thread
drives industry standard 640x480 @ 60 Hz VGA signalling (hsync, vsync and RGB pixel
data) directly from xcore ports, streaming pixels from a frame buffer in memory. A
simple resistor DAC is all the external hardware required. Although VGA is a legacy
standard, VGA to HDMI adapters are commercially available at very low cost, allowing
use with modern displays.

********
Features
********

* Industry standard 640x480 @ 60 Hz VGA signal timing (25.000 MHz pixel clock derived
  from the 100 MHz reference clock, giving a 59.52 Hz frame rate - within display
  tolerance of the nominal 25.175 MHz / 59.94 Hz)
* 640x480 and 320x240 (pixel doubled) frame buffer resolutions
* 4 bpp (IRGB), 8 bpp (RGB332) and 16 bpp (RGB565) pixel formats, selected by the
  width of the RGB port
* Single thread operation using timed port outputs - no CPU cycles wasted during blanking
* Verified at the minimum guaranteed thread speed of 75 MHz (600 MHz / 8 threads) for
  all modes except 640x480 16 bpp, which requires a 120 MHz thread
* Frame buffer can be swapped at any time by updating a single pointer, with no tearing

************
Known issues
************

* None

****************
Development repo
****************

* `lib_vga <https://www.github.com/xmos/lib_vga>`_ (https://www.github.com/xmos/lib_vga)

**************
Required tools
**************

* XMOS XTC Tools: 15.3.1

*********************************
Required libraries (dependencies)
*********************************

* None

*************************
Related application notes
*************************

* None

*******
Support
*******

This package is supported by XMOS Ltd. Issues can be raised against the software at
`www.xmos.com/support <https://www.xmos.com/support>`_ or using GitHub `issues <https://github.com/xmos/lib_vga/issues>`_.
