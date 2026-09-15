#pragma once

#include <driver/spi_master.h>

#include "common.h"

constexpr int16_t NOT_CONNECTED = -1;

// pixel data pins
constexpr uint8_t CAMERA_D0 = 1;
constexpr uint8_t CAMERA_D1 = 2;
constexpr uint8_t CAMERA_D2 = 9;
constexpr uint8_t CAMERA_D3 = 10;
constexpr uint8_t CAMERA_D4 = 11;
constexpr uint8_t CAMERA_D5 = 12;
constexpr uint8_t CAMERA_D6 = 13;
constexpr uint8_t CAMERA_D7 = 14;

// control and sync pins
constexpr uint8_t CAMERA_SDA   = 4;
constexpr uint8_t CAMERA_SCL   = 5;
constexpr uint8_t CAMERA_VSYNC = 6;
constexpr uint8_t CAMERA_HREF  = 7;
constexpr uint8_t CAMERA_PCLK  = 15;  // pixel clock

// SPI pins
constexpr uint8_t SPI_SCK  = 39;  // for TFT and SD
constexpr uint8_t SPI_MOSI = 40;  // for TFT (SDA) and SD
constexpr uint8_t SPI_MISO = 48;  // for SD only

// display pins
constexpr uint8_t DISPLAY_DC = 41;
constexpr uint8_t DISPLAY_CS = 42;

// SD card pins
constexpr uint8_t SD_CS = 47;

// button pins
constexpr uint8_t BTN_A = 21;  // up
constexpr uint8_t BTN_B = 20;  // down
constexpr uint8_t BTN_X = 19;  // select

constexpr uint8_t BTN_SHUTTER = 16;  // (not used rn)

// size in landscape orientation
constexpr uint16_t DISPLAY_WIDTH  = 320;
constexpr uint16_t DISPLAY_HEIGHT = 240;

// countdown params
constexpr uint16_t TICK_COUNT     = 3;
constexpr uint32_t TICK_TIME      = 1000;
constexpr uint32_t TICK_COUNT_MAX = 250;

// camera params
constexpr uint8_t CAMERA_JPEG_QUALITY = 85;
// constexpr uint8_t CAMERA_JPEG_QUALITY = 10;  // 0-63, lower means higher quality

// data transfer rate
constexpr uint32_t DISPLAY_FREQ_WRITE = 80_MHz;  // pizdets
constexpr uint32_t DISPLAY_FREQ_READ  = 16_MHz;
constexpr uint32_t CAMERA_PCLK_FREQ   = 20_MHz;
constexpr uint32_t SD_CARD_SPI_FREQ   = 10_MHz;

// SPI
constexpr spi_host_device_t SPI_HOST = SPI2_HOST;
