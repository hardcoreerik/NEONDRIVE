#pragma once

#include <Arduino.h>
#include <WiFi.h>

static constexpr int MAX_APS = 60;

struct ApRecord {
  String ssid;
  String bssid;
  int rssi;
  int channel;
  wifi_auth_mode_t auth;
};
