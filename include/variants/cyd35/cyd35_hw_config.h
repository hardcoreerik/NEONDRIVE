#pragma once

// 3.5" CYD hardware revisions can differ. Select revision at build time:
//   -DNEONDRIVE_CYD35_REV_A  (default)
//   -DNEONDRIVE_CYD35_REV_B
#if !defined(NEONDRIVE_CYD35_REV_A) && !defined(NEONDRIVE_CYD35_REV_B)
#define NEONDRIVE_CYD35_REV_A 1
#endif

#if defined(NEONDRIVE_CYD35_REV_B)
// Alternate placeholder mapping for known clone differences.
#define CYD35_TFT_MISO 12
#define CYD35_TFT_MOSI 13
#define CYD35_TFT_SCLK 14
#define CYD35_TFT_CS   15
#define CYD35_TFT_DC   2
#define CYD35_TFT_RST  -1
#define CYD35_TOUCH_CS 32
#define CYD35_BL_PIN_0 21
#define CYD35_BL_PIN_1 -1
#else
// Revision A default (same ESP32 CYD SPI bus; common 3.5 touch pin CS=33)
#define CYD35_TFT_MISO 12
#define CYD35_TFT_MOSI 13
#define CYD35_TFT_SCLK 14
#define CYD35_TFT_CS   15
#define CYD35_TFT_DC   2
#define CYD35_TFT_RST  -1
#define CYD35_TOUCH_CS 33
#define CYD35_BL_PIN_0 21
#define CYD35_BL_PIN_1 27
#endif

#define CYD35_SD_SCLK 18
#define CYD35_SD_MISO 19
#define CYD35_SD_MOSI 23
#define CYD35_SD_CS   5

#define CYD35_SW1     0

// GPS UART (Serial2) — connect GPS TX → GPIO16, GPS RX ← GPIO17 (tx, optional)
#define CYD35_GPS_RX  16
#define CYD35_GPS_TX  17
