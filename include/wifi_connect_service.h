#ifndef WIFI_CONNECT_SERVICE_H
#define WIFI_CONNECT_SERVICE_H

#include <Arduino.h>

#include "app_config.h"

using WifiConsoleEventf = void (*)(const char* level, const char* tag, const char* fmt, ...);
using WifiUiRefreshFn = void (*)();

void wifiSaveCredential(AppConfig& cfg, const String& ssid, const String& password);

void wifiConnectStart(const String& ssid,
                      const String& password,
                      String& wifiConnectStatus,
                      bool& wifiConnectInProgress,
                      uint32_t& wifiConnectAttemptMs,
                      String& wifiConnectSsid,
                      String& wifiConnectPassword,
                      WifiConsoleEventf pushConsoleEventfFn);

void wifiConnectTick(bool isWifiConnectScreen,
                     bool& wifiConnectInProgress,
                     String& wifiConnectStatus,
                     uint32_t& wifiConnectAttemptMs,
                     const String& wifiConnectSsid,
                     const String& wifiConnectPassword,
                     AppConfig& cfg,
                     WifiUiRefreshFn drawWifiConnectFn,
                     WifiUiRefreshFn refreshWifiStatusLineFn,
                     WifiConsoleEventf pushConsoleEventfFn);

#endif // WIFI_CONNECT_SERVICE_H
