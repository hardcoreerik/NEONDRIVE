// TFT_eSPI User Setup for LilyGO T-Embed CC1101 Plus
// Panel: ST7789 170x320 on shared SPI bus (MOSI=9, MISO=10, SCK=11)
// Pins verified from: examples/factory_test/utilities.h + Setup214_LilyGo_T_Embed_PN532.h
// Official repo: https://github.com/Xinyuan-LilyGO/T-Embed-CC1101

#ifndef USER_SETUP_TEMBED_CC1101_H
#define USER_SETUP_TEMBED_CC1101_H

#define ST7789_DRIVER

#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_ON

#define TFT_WIDTH  170
#define TFT_HEIGHT 320

// Shared SPI bus — all SPI devices use GPIO 9/10/11
#define TFT_MOSI  9
#define TFT_MISO  10
#define TFT_SCLK  11

#define TFT_CS    41   // DISPLAY_CS
#define TFT_DC    16
#define TFT_RST   40   // DISPLAY_RST

#define TFT_BL    21   // DISPLAY_BL
#define TFT_BACKLIGHT_ON HIGH

#define SPI_FREQUENCY       80000000
#define SPI_READ_FREQUENCY  20000000

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SMOOTH_FONT

#endif  // USER_SETUP_TEMBED_CC1101_H
