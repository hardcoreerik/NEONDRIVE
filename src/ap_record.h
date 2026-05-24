#pragma once

#include <Arduino.h>
#include <WiFi.h>

static constexpr int MAX_APS = 60;

struct ApRecord {
  String ssid;
  String bssid;
  int rssi    = 0;
  int channel = 0;
  wifi_auth_mode_t auth = WIFI_AUTH_OPEN;
  bool    wpsEnabled  = false; // WPS IE detected
  bool    wpsLocked   = false; // AP Setup Locked attribute
  uint8_t wpsVersion  = 0;     // 0x10=WPS1.0, 0x20=WPS2.0, 0=unknown
};
