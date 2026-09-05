#pragma once

constexpr int16_t NOT_CONNECTED = -1;

// camera pixel data
constexpr uint8_t CAM_D0 = 1;
constexpr uint8_t CAM_D1 = 2;
constexpr uint8_t CAM_D2 = 9;
constexpr uint8_t CAM_D3 = 10;
constexpr uint8_t CAM_D4 = 11;
constexpr uint8_t CAM_D5 = 12;
constexpr uint8_t CAM_D6 = 13;
constexpr uint8_t CAM_D7 = 14;

// camera control and sync
constexpr uint8_t CAM_SDA   = 4;
constexpr uint8_t CAM_SCL   = 5;
constexpr uint8_t CAM_VSYNC = 6;
constexpr uint8_t CAM_HREF  = 7;
constexpr uint8_t CAM_PCLK  = 15;  // pixel clock

// SPI
constexpr uint8_t SPI_SCK  = 39;  // for TFT and SD
constexpr uint8_t SPI_MOSI = 40;  // for TFT (SDA) and SD
constexpr uint8_t SPI_MISO = 48;  // for SD only

// display
constexpr uint8_t TFT_DC = 41;
constexpr uint8_t TFT_CS = 42;

// SD card
constexpr uint8_t SD_CS = 47;

// buttons
constexpr uint8_t BTN_A = 21;  // up
constexpr uint8_t BTN_B = 20;  // down
constexpr uint8_t BTN_X = 19;  // select

constexpr uint8_t BTN_SHUTTER = 16;  // (not used rn)

// size in landscape orientation
constexpr uint16_t DISPLAY_WIDTH  = 320;
constexpr uint16_t DISPLAY_HEIGHT = 240;

// countdown ticks
constexpr uint16_t TICK_COUNT = 3;
constexpr uint32_t TICK_TIME  = 750;

// data transfer frequency
constexpr uint32_t TFT_FREQ_WRITE = 80_MHz;  // pizdets
constexpr uint32_t TFT_FREQ_READ  = 16_MHz;
constexpr uint32_t CAM_PCLK_FREQ  = 20_MHz;
