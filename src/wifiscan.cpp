#include "wifiscan_helpers.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

// Implementations moved from main.cpp
static bool s_asyncScanActive = false;
static uint8_t s_asyncRetryCount = 0;

static void resetScanStorage() {
  apCount = 0;
  apScroll = 0;
}

static void collectScanResults(int n) {
  resetScanStorage();
  if (n < 0) {
    Serial.printf("[wifi] scan failed: %d\n", n);
    wifiIsScanning = false;
    return;
  }

  for (int i = 0; i < n && apCount < MAX_APS; i++) {
    String bssid = WiFi.BSSIDstr(i);
    int existing = -1;
    for (int j = 0; j < apCount; j++) {
      if (bssidEquals(aps[j].bssid, bssid)) {
        existing = j;
        break;
      }
    }

    ApRecord r;
    r.ssid = WiFi.SSID(i);
    if (r.ssid.length() == 0) r.ssid = "(hidden)";
    r.bssid = bssid;
    r.rssi = WiFi.RSSI(i);
    r.channel = WiFi.channel(i);
    r.auth = WiFi.encryptionType(i);

    if (existing >= 0) {
      if (r.rssi > aps[existing].rssi) aps[existing] = r;
    } else {
      aps[apCount++] = r;
    }
  }

  dedupeKeepStrongest();
  sortApsByRssiDesc();
  Serial.printf("[wifi] scan done. found=%d stored=%d\n", n, apCount);
  wifiIsScanning = false;
}

static bool prepareScanEnvironment() {
  // Ensure we are out of monitor/AP state before scanning.
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  int mode = WiFi.getMode();
  if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
    WiFi.softAPdisconnect(true);
  }

  // Ensure STA mode without forcing full driver deinit.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(50);
  WiFi.scanDelete();
  return true;
}

bool bssidEquals(const String& a, const String& b) {
  return a.equalsIgnoreCase(b);
}

void sortApsByRssiDesc() {
  // Simple selection sort; MAX_APS is small.
  for (int i = 0; i < apCount - 1; i++) {
    int best = i;
    for (int j = i + 1; j < apCount; j++) {
      if (aps[j].rssi > aps[best].rssi) best = j;
    }
    if (best != i) {
      ApRecord tmp = aps[i];
      aps[i] = aps[best];
      aps[best] = tmp;
    }
  }
}

void dedupeKeepStrongest() {
  // O(n^2) dedupe by BSSID, keep strongest RSSI entry.
  for (int i = 0; i < apCount; i++) {
    for (int j = i + 1; j < apCount; ) {
      if (bssidEquals(aps[i].bssid, aps[j].bssid)) {
        if (aps[j].rssi > aps[i].rssi) aps[i] = aps[j];
        // remove j by shifting
        for (int k = j; k < apCount - 1; k++) aps[k] = aps[k + 1];
        apCount--;
      } else {
        j++;
      }
    }
  }
}

void doWifiScanBlocking() {
  wifiIsScanning = true;
  s_asyncScanActive = false;
  s_asyncRetryCount = 0;
  prepareScanEnvironment();

  Serial.println("[wifi] scan start (blocking) ...");

  int n = WiFi.scanNetworks(false, true); // (async=false, show_hidden=true)
  if (n < 0) {
    // One-shot recovery path for low-level init failures.
    Serial.printf("[wifi] first scan attempt failed: %d, retrying init\n", n);
    WiFi.mode(WIFI_OFF);
    delay(80);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    delay(80);
    WiFi.scanDelete();
    n = WiFi.scanNetworks(false, true);
  }

  collectScanResults(n);
}

bool startWifiScanAsync() {
  wifiIsScanning = true;
  s_asyncRetryCount = 0;
  prepareScanEnvironment();
  Serial.println("[wifi] scan start (async) ...");
  int r = WiFi.scanNetworks(true, true); // async=true, show_hidden=true
  if (r == WIFI_SCAN_FAILED) {
    Serial.println("[wifi] async scan start failed");
    wifiIsScanning = false;
    s_asyncScanActive = false;
    return false;
  }
  if (r >= 0) {
    // Some stacks may return immediate results.
    collectScanResults(r);
    s_asyncScanActive = false;
    return true;
  }
  s_asyncScanActive = true;
  return true;
}

int pollWifiScanAsync() {
  if (!s_asyncScanActive) return 0;

  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return 0;
  if (n == WIFI_SCAN_FAILED) {
    if (s_asyncRetryCount == 0) {
      s_asyncRetryCount = 1;
      Serial.println("[wifi] async scan failed, retrying once");
      prepareScanEnvironment();
      int r = WiFi.scanNetworks(true, true);
      if (r == WIFI_SCAN_RUNNING) return 0;
      if (r >= 0) {
        s_asyncScanActive = false;
        collectScanResults(r);
        return 1;
      }
    }
    s_asyncScanActive = false;
    wifiIsScanning = false;
    return -1;
  }

  s_asyncScanActive = false;
  collectScanResults(n);
  return 1;
}

bool wifiStaHasValidIp() {
  if (WiFi.status() != WL_CONNECTED) return false;
  IPAddress ip = WiFi.localIP();
  return !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

bool wifiGetStaNetwork(IPAddress& localIp, IPAddress& subnetMask, IPAddress& gateway) {
  if (!wifiStaHasValidIp()) return false;
  localIp = WiFi.localIP();
  subnetMask = WiFi.subnetMask();
  gateway = WiFi.gatewayIP();
  return true;
}
