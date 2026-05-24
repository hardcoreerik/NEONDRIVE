#pragma once

// Select active compile-time device profile.
#if defined(NEONDRIVE_TARGET_M5TAB5)
  #include "device_profiles/profile_m5tab5.h"
#elif defined(NEONDRIVE_TARGET_T_EMBED_CC1101)
  #include "device_profiles/profile_t_embed_cc1101.h"
#elif defined(NEONDRIVE_TARGET_TDISPLAY_S3)
  #include "device_profiles/profile_t_display_s3.h"
#elif defined(NEONDRIVE_TARGET_CYD35)
  #include "device_profiles/profile_cyd_3_5.h"
#elif defined(NEONDRIVE_TARGET_CYD28)
  #include "device_profiles/profile_cyd_2_8.h"
#else
  #include "device_profiles/profile_cyd_2_4.h"
#endif

// Safety defaults for optional capability flags.
#ifndef ND_PROFILE_CODE
#define ND_PROFILE_CODE "unknown"
#endif
#ifndef ND_PROFILE_NAME
#define ND_PROFILE_NAME "NEONDRIVE // Unknown"
#endif
#ifndef ND_HOME_HEADER
#define ND_HOME_HEADER "NEONDRIVE // Unknown"
#endif
#ifndef ND_DISPLAY_W
#define ND_DISPLAY_W 320
#endif
#ifndef ND_DISPLAY_H
#define ND_DISPLAY_H 240
#endif
#ifndef ND_HW_TOUCH
#define ND_HW_TOUCH 0
#endif
#ifndef ND_HW_BUTTON_NAV
#define ND_HW_BUTTON_NAV 0
#endif
#ifndef ND_HW_ENCODER
#define ND_HW_ENCODER 0
#endif
#ifndef ND_HW_KEYBOARD
#define ND_HW_KEYBOARD 0
#endif
#ifndef ND_HW_CC1101
#define ND_HW_CC1101 0
#endif
#ifndef ND_HW_SD
#define ND_HW_SD 0
#endif
#ifndef ND_FLASH_MB
#define ND_FLASH_MB 0
#endif
#ifndef ND_EXPECT_PSRAM
#define ND_EXPECT_PSRAM 0
#endif
#ifndef ND_FEATURE_WEB_UI
#define ND_FEATURE_WEB_UI 1
#endif
#ifndef ND_FEATURE_SCOPE_WATERFALL
#define ND_FEATURE_SCOPE_WATERFALL 1
#endif
#ifndef ND_FEATURE_SD_CAPTURE
#define ND_FEATURE_SD_CAPTURE 0
#endif
#ifndef ND_FEATURE_SUBGHZ_TOOLKIT
#define ND_FEATURE_SUBGHZ_TOOLKIT 0
#endif
