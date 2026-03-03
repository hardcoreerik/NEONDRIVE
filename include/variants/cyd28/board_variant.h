#pragma once

// CYD 2.8 baseline variant (ESP32-2432S028R family)
#define CYD_VARIANT_NAME "CYD28"

// TFT (ILI9341 SPI)
static constexpr int CYD_PIN_TFT_SCLK = 14;
static constexpr int CYD_PIN_TFT_MISO = 12;
static constexpr int CYD_PIN_TFT_MOSI = 13;
static constexpr int CYD_PIN_TFT_CS   = 15;

// Touch (XPT2046 on dedicated pins on many CYD 2.8 boards)
static constexpr int CYD_PIN_TOUCH_CS = 33;
static constexpr int CYD_PIN_TOUCH_SPI_SCLK = 25;
static constexpr int CYD_PIN_TOUCH_SPI_MISO = 39;
static constexpr int CYD_PIN_TOUCH_SPI_MOSI = 32;
static constexpr int CYD_PIN_TOUCH_IRQ = 36;
static constexpr bool CYD_TOUCH_SEPARATE_SPI = true;

// SD card
static constexpr int CYD_PIN_SD_SCLK = 18;
static constexpr int CYD_PIN_SD_MISO = 19;
static constexpr int CYD_PIN_SD_MOSI = 23;
static constexpr int CYD_PIN_SD_CS   = 5;

static constexpr int CYD_PIN_SW1 = 0;

// CYD 2.8 usually has a single TFT backlight pin.
static constexpr int CYD_BL_PINS[] = {21, -1};
static constexpr uint8_t CYD_BL_PWM_CHANNELS[] = {4, 5};

static constexpr int CYD_LED_PINS[] = {4, 16, 17};
static constexpr uint8_t CYD_LED_PWM_CHANNELS[] = {0, 1, 2};

// Conservative defaults; user can calibrate if needed.
static constexpr int CYD_TOUCH_RAW_X_MIN = 250;
static constexpr int CYD_TOUCH_RAW_X_MAX = 3850;
static constexpr int CYD_TOUCH_RAW_Y_MIN = 250;
static constexpr int CYD_TOUCH_RAW_Y_MAX = 3850;
static constexpr int CYD_TOUCH_Z_MIN = 1;

static constexpr bool CYD_TOUCH_SWAP_XY = false;
static constexpr bool CYD_TOUCH_INVERT_X = false;
static constexpr bool CYD_TOUCH_INVERT_Y = false;

static constexpr int CYD_TFT_ROTATION = 1;
// CYD 2.8 panels are typically correct with inversion disabled.
static constexpr bool CYD_TFT_INVERT = false;
