#pragma once

// NEONDRIVE CYD 3.5" TFT_eSPI setup (ESP32 SPI + XPT2046 touch)
#include "variants/cyd35/cyd35_hw_config.h"

#ifndef USER_SETUP_H
#define USER_SETUP_H

// ESP32-3248S035R commonly uses ST7796 + XPT2046.
#define ST7796_DRIVER
#define TFT_WIDTH  320
#define TFT_HEIGHT 480
#define TFT_RGB_ORDER TFT_BGR

#define TFT_MISO CYD35_TFT_MISO
#define TFT_MOSI CYD35_TFT_MOSI
#define TFT_SCLK CYD35_TFT_SCLK

#define TFT_CS   CYD35_TFT_CS
#define TFT_DC   CYD35_TFT_DC
#define TFT_RST  CYD35_TFT_RST

#define SPI_FREQUENCY       24000000
#define SPI_READ_FREQUENCY  16000000
#define SPI_TOUCH_FREQUENCY  2500000

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#endif
