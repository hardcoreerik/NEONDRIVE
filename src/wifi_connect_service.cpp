#include "wifi_connect_service.h"

#include <WiFi.h>

#include "config_store.h"

void wifiSaveCredential(AppConfig& cfg, const String& ssid, const String& password) {
  if (ssid.isEmpty()) return;

  int existing = -1;
  for (int i = 0; i < cfg.wifi_savedCount; i++) {
    if (cfg.wifi_savedSsid[i] == ssid) {
      existing = i;
      break;
    }
  }

  if (existing > 0) {
    String keepSsid = cfg.wifi_savedSsid[existing];
    String keepPass = cfg.wifi_savedPassword[existing];
    for (int i = existing; i > 0; i--) {
      cfg.wifi_savedSsid[i] = cfg.wifi_savedSsid[i - 1];
      cfg.wifi_savedPassword[i] = cfg.wifi_savedPassword[i - 1];
    }
    cfg.wifi_savedSsid[0] = keepSsid;
    cfg.wifi_savedPassword[0] = keepPass;
  } else if (existing == -1) {
    int newCount = cfg.wifi_savedCount;
    if (newCount < AppConfig::MAX_SAVED_WIFI) newCount++;
    for (int i = newCount - 1; i > 0; i--) {
      cfg.wifi_savedSsid[i] = cfg.wifi_savedSsid[i - 1];
      cfg.wifi_savedPassword[i] = cfg.wifi_savedPassword[i - 1];
    }
    cfg.wifi_savedSsid[0] = ssid;
    cfg.wifi_savedPassword[0] = password;
    cfg.wifi_savedCount = newCount;
  }

  cfg.wifi_ssid = ssid;
  cfg.wifi_password = password;
  if (saveConfig(cfg)) {
    Serial.printf("[cfg] saved WiFi credential for '%s' (saved=%d)\n", ssid.c_str(), cfg.wifi_savedCount);
  }
}

void wifiConnectStart(const String& ssid,
                      const String& password,
                      String& wifiConnectStatus,
                      bool& wifiConnectInProgress,
                      uint32_t& wifiConnectAttemptMs,
                      String& wifiConnectSsid,
                      String& wifiConnectPassword,
                      WifiConsoleEventf pushConsoleEventfFn) {
  if (ssid.isEmpty()) {
    wifiConnectStatus = "No SSID selected";
    Serial.println("[wifi] Connect attempt: no SSID selected");
    return;
  }

  wifiConnectInProgress = true;
  wifiConnectStatus = "Connecting...";
  wifiConnectAttemptMs = millis();
  wifiConnectSsid = ssid;
  wifiConnectPassword = password;
  if (pushConsoleEventfFn) {
    pushConsoleEventfFn("INFO", "WIFI", "Connect request ssid='%s'", ssid.c_str());
  }

  Serial.printf("[wifi] Attempting STA connection to '%s'\n", ssid.c_str());

  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(100);

  if (password.isEmpty()) {
    Serial.println("[wifi] Connecting without password (open network)");
    WiFi.begin(ssid.c_str());
  } else {
    Serial.printf("[wifi] Connecting with password (%d chars)\n", password.length());
    WiFi.begin(ssid.c_str(), password.c_str());
  }
}

void wifiConnectTick(bool isWifiConnectScreen,
                     bool& wifiConnectInProgress,
                     String& wifiConnectStatus,
                     uint32_t& wifiConnectAttemptMs,
                     const String& wifiConnectSsid,
                     const String& wifiConnectPassword,
                     AppConfig& cfg,
                     WifiUiRefreshFn drawWifiConnectFn,
                     WifiUiRefreshFn refreshWifiStatusLineFn,
                     WifiConsoleEventf pushConsoleEventfFn) {
  if (!wifiConnectInProgress) return;

  int wifiStatus = WiFi.status();
  bool stateChanged = false;

  if (wifiStatus == WL_CONNECTED) {
    wifiConnectStatus = "Connected!";
    wifiConnectInProgress = false;
    stateChanged = true;
    wifiSaveCredential(cfg, wifiConnectSsid, wifiConnectPassword);
    Serial.printf("[wifi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    if (pushConsoleEventfFn) {
      pushConsoleEventfFn("INFO", "WIFI", "Connected ssid='%s' ip=%s",
                          wifiConnectSsid.c_str(), WiFi.localIP().toString().c_str());
    }
  } else if (wifiStatus == WL_CONNECT_FAILED) {
    wifiConnectStatus = "Connect failed";
    wifiConnectInProgress = false;
    stateChanged = true;
    if (pushConsoleEventfFn) {
      pushConsoleEventfFn("ERROR", "WIFI", "Connect failed ssid='%s'", wifiConnectSsid.c_str());
    }
  } else if (wifiStatus == WL_NO_SSID_AVAIL) {
    wifiConnectStatus = "SSID not found";
    wifiConnectInProgress = false;
    stateChanged = true;
    if (pushConsoleEventfFn) {
      pushConsoleEventfFn("WARN", "WIFI", "SSID not found '%s'", wifiConnectSsid.c_str());
    }
  } else if (wifiStatus == WL_DISCONNECTED) {
    uint32_t elapsed = millis() - wifiConnectAttemptMs;
    if (elapsed > 15000) {
      wifiConnectStatus = "Connection timeout";
      wifiConnectInProgress = false;
      stateChanged = true;
      WiFi.disconnect();
      if (pushConsoleEventfFn) {
        pushConsoleEventfFn("WARN", "WIFI", "Connect timeout ssid='%s'", wifiConnectSsid.c_str());
      }
    }
  }

  if (!isWifiConnectScreen) return;
  if (stateChanged) {
    if (drawWifiConnectFn) drawWifiConnectFn();
    return;
  }

  static uint32_t lastStatusRefreshMs = 0;
  if (millis() - lastStatusRefreshMs > 500) {
    lastStatusRefreshMs = millis();
    if (refreshWifiStatusLineFn) refreshWifiStatusLineFn();
  }
}
