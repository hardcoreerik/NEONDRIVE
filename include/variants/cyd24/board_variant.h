#pragma once

// CYD 2.4 baseline variant (locked behavior)
#define CYD_VARIANT_NAME "CYD24"

static constexpr int CYD_PIN_TFT_SCLK = 14;
static constexpr int CYD_PIN_TFT_MISO = 12;
static constexpr int CYD_PIN_TFT_MOSI = 13;
static constexpr int CYD_PIN_TFT_CS   = 15;

static constexpr int CYD_PIN_TOUCH_CS = 33;

static constexpr int CYD_PIN_SD_SCLK = 18;
static constexpr int CYD_PIN_SD_MISO = 19;
static constexpr int CYD_PIN_SD_MOSI = 23;
static constexpr int CYD_PIN_SD_CS   = 5;

static constexpr int CYD_PIN_SW1 = 0;

static constexpr int CYD_BL_PINS[] = {21, 27};
static constexpr uint8_t CYD_BL_PWM_CHANNELS[] = {4, 5};

static constexpr int CYD_LED_PINS[] = {4, 16, 17};
static constexpr uint8_t CYD_LED_PWM_CHANNELS[] = {0, 1, 2};

static constexpr int CYD_TOUCH_RAW_X_MIN = 250;
static constexpr int CYD_TOUCH_RAW_X_MAX = 3850;
static constexpr int CYD_TOUCH_RAW_Y_MIN = 250;
static constexpr int CYD_TOUCH_RAW_Y_MAX = 3850;
static constexpr int CYD_TOUCH_Z_MIN = 80;

static constexpr bool CYD_TOUCH_SWAP_XY = false;
static constexpr bool CYD_TOUCH_INVERT_X = true;
static constexpr bool CYD_TOUCH_INVERT_Y = false;

static constexpr int CYD_TFT_ROTATION = 1;
static constexpr bool CYD_TFT_INVERT = true;
