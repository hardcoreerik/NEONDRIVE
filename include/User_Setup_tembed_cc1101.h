// TFT_eSPI User Setup for LilyGO T-Embed CC1101
// Panel: ST7789 170x320 on SPI bus

#ifndef USER_SETUP_TEMBED_CC1101_H
#define USER_SETUP_TEMBED_CC1101_H

#define ST7789_DRIVER
#define INIT_SEQUENCE_3

#define TFT_RGB_ORDER TFT_RGB
#define TFT_INVERSION_ON
#define CGRAM_OFFSET

#define TFT_WIDTH 170
#define TFT_HEIGHT 320

#define TFT_MISO -1
#define TFT_MOSI 42
#define TFT_SCLK 40

#define TFT_CS   45
#define TFT_DC   41
#define TFT_RST  47

#define TFT_BL 21
#define TFT_BACKLIGHT_ON HIGH

#define SPI_FREQUENCY       40000000
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
