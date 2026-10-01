# `open-camera`

— Hackable, extendable, high performance OV2640 digital camera, made for circuit bending experiments

### Features

- Custom Glitch Matrix for unique circuit bending effects
- Smooth 30FPS live preview viewfinder
- Taking multiple resolution frames within a single shot, one by one
- Saving JPEG (and optionally RAW) images to an SD card
- Changing settings on the fly in the menu:
  - Brightness, contrast, saturation, white balance, special effects (sensor-side)
  - Resolution multi-selection: 240p, 480p, 600p, 768p, 1200p
  - Horizontal and vertical mirroring, on screen overlay

### What is the Glitch Matrix?

Camera module has DVP (Digital Video Port) interface with lines D0-D7 — Glitch Matrix allows you to perform the following operations on them:

- Disconnect any data line
- Bend any data line in any way
  - Merge two outputs into one input
  - Split one inpit into two outputs
  - Cross two data lines

This allows you to create crazy glitch art effects — the whole process is called [Circuit Bending](https://en.wikipedia.org/wiki/Circuit_bending) ([examples on Reddit](https://www.reddit.com/r/CircuitBending))

The sensitive camera module is protected by an array of current-limiting resistors, so bending can be done safely on live camera

### Components

- ESP32-S3 N16R8 240 MHz Dual Core
- OV2640 2MP Camera Module (red board)
- 320x240 TFT LCD SPI Display on ST7789 chip
- SD card slot + SD card
- 8 pin DIP switch
- 8x 10Ω Resistors
- 3x Buttons
- 2x 18650 cell
- 5V 3A UPS power module

Fast ESP32-S3 with a lot of PSRAM (8MB) is required because JPEG compression need to happen on the device, not on the sensor, and full UXGA RGB565 frame buffer is around 4MB

### TODO

- Browsing captured images (already saving RAW thumbs for that)
- RGB LED ring as a progress indicator or flashlight
- Better pinout (avoid sharing same SPI bus for display and SD)
- Improve README (add pinout, example images, more technical details)
