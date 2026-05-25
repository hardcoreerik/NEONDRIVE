#pragma once

// NEONDRIVE // M5Stack Cardputer Advanced profile (ESP32-S3, 8MB flash, no PSRAM)
#define ND_PROFILE_STATUS "alpha"
#define ND_PROFILE_CODE   "m5cardputer"
#define ND_PROFILE_NAME   "NEONDRIVE // M5 Cardputer Adv [ALPHA]"
#define ND_HOME_HEADER    "NEONDRIVE // CARDPUTER [ALPHA]"

#define ND_DISPLAY_W 240
#define ND_DISPLAY_H 135

// No touch screen — navigation via 56-key mechanical keyboard (TCA8418 I²C)
#define ND_HW_TOUCH       0
#define ND_HW_BUTTON_NAV  1   // keyboard provides button navigation
#define ND_HW_ENCODER     0
#define ND_HW_KEYBOARD    1
#define ND_HW_CC1101      0   // no built-in sub-GHz; optional via EXT header
#define ND_HW_SD          1   // SPI: SCK=40, MISO=39, MOSI=14, CS=12

#define ND_FLASH_MB      8
#define ND_EXPECT_PSRAM  0

#define ND_FEATURE_WEB_UI           1
#define ND_FEATURE_SCOPE_WATERFALL  0   // 240×135 too small for waterfall
#define ND_FEATURE_SD_CAPTURE       1
#define ND_FEATURE_SUBGHZ_TOOLKIT   0
