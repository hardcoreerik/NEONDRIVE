#pragma once

#include "cyd35_hw_config.h"

#define CYD_VARIANT_NAME "CYD35"

static constexpr int CYD_PIN_TFT_SCLK = CYD35_TFT_SCLK;
static constexpr int CYD_PIN_TFT_MISO = CYD35_TFT_MISO;
static constexpr int CYD_PIN_TFT_MOSI = CYD35_TFT_MOSI;
static constexpr int CYD_PIN_TFT_CS   = CYD35_TFT_CS;

static constexpr int CYD_PIN_TOUCH_CS = CYD35_TOUCH_CS;

static constexpr int CYD_PIN_SD_SCLK = CYD35_SD_SCLK;
static constexpr int CYD_PIN_SD_MISO = CYD35_SD_MISO;
static constexpr int CYD_PIN_SD_MOSI = CYD35_SD_MOSI;
static constexpr int CYD_PIN_SD_CS   = CYD35_SD_CS;

static constexpr int CYD_PIN_SW1 = CYD35_SW1;

static constexpr int CYD_BL_PINS[] = {CYD35_BL_PIN_0, CYD35_BL_PIN_1};
static constexpr uint8_t CYD_BL_PWM_CHANNELS[] = {4, 5};

static constexpr int CYD_LED_PINS[] = {4, 16, 17};
static constexpr uint8_t CYD_LED_PWM_CHANNELS[] = {0, 1, 2};

// Default calibration for common 3.5 XPT2046 modules (adjust if test indicates drift).
static constexpr int CYD_TOUCH_RAW_X_MIN = 180;
static constexpr int CYD_TOUCH_RAW_X_MAX = 3920;
static constexpr int CYD_TOUCH_RAW_Y_MIN = 180;
static constexpr int CYD_TOUCH_RAW_Y_MAX = 3000;
static constexpr int CYD_TOUCH_Z_MIN = 80;

// Match CYD24 touch layout exactly.
static constexpr bool CYD_TOUCH_SWAP_XY = true;
static constexpr bool CYD_TOUCH_INVERT_X = true;
static constexpr bool CYD_TOUCH_INVERT_Y = true;

static constexpr int CYD_TFT_ROTATION = 1;
static constexpr bool CYD_TFT_INVERT = false;
