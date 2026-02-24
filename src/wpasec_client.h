#pragma once

#include <Arduino.h>
#include <FS.h>

struct WPASecSyncResult {
  bool success = false;
  uint16_t uploaded = 0;
  uint16_t failed = 0;
  uint16_t skipped = 0;
  uint16_t cracked = 0;
  uint16_t newCracked = 0;
  char error[64] = {0};
};

typedef void (*WPASecProgressCallback)(const char* status, uint16_t progress, uint16_t total);

namespace WPASecClient {
bool hasApiKey(const String& key);
bool loadCache(fs::FS& fs, const char* resultsPath, const char* uploadedPath);
bool isCracked(const char* bssid);
const char* getPassword(const char* bssid);
const char* getSsid(const char* bssid);
uint16_t crackedCount();
const char* lastError();
bool isBusy();
void freeCacheMemory();
WPASecSyncResult syncCaptures(fs::FS& fs,
                              const char* capturesRoot,
                              const char* uploadedPath,
                              const char* resultsPath,
                              const String& apiKey,
                              WPASecProgressCallback cb = nullptr);
}

