#pragma once

// NEONDRIVE // T-Embed CC1101 profile (ESP32-S3, 16MB flash + PSRAM)
// ⚠️ STATUS: UNTESTED — compiled but not validated on physical hardware.
#define ND_PROFILE_STATUS "untested"
#define ND_PROFILE_CODE "t_embed_cc1101"
#define ND_PROFILE_NAME "NEONDRIVE // T-Embed CC1101 [UNTESTED]"
#define ND_HOME_HEADER  "NEONDRIVE // T-EMBED CC1101 [UNTESTED]"

#define ND_DISPLAY_W 320
#define ND_DISPLAY_H 170

#define ND_HW_TOUCH 0
#define ND_HW_BUTTON_NAV 1
#define ND_HW_ENCODER 1
#define ND_HW_KEYBOARD 0
#define ND_HW_CC1101 1
#define ND_HW_SD 1

#define ND_HW_NRF24         1
#define ND_HW_IR_TX         1
#define ND_HW_IR_RX         1
#define ND_HW_WS2812        1
#define ND_HW_ROTARY_BUTTON 1
#define ND_HW_BATTERY_ADC   0

#define ND_FLASH_MB 16
#define ND_EXPECT_PSRAM 1

#define ND_FEATURE_WEB_UI 1
#define ND_FEATURE_SCOPE_WATERFALL 1
#define ND_FEATURE_SD_CAPTURE 0
#define ND_FEATURE_SUBGHZ_TOOLKIT 1
