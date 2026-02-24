#pragma once

#include <Arduino.h>
#include <FS.h>

struct WigleSyncResult {
  bool success = false;
  uint16_t uploaded = 0;
  uint16_t failed = 0;
  uint16_t skipped = 0;
  bool statsFetched = false;
  char error[64] = {0};
};

struct WigleUserStats {
  bool valid = false;
  int64_t rank = 0;
  uint64_t wifi = 0;
  uint64_t cell = 0;
  uint64_t bt = 0;
};

typedef void (*WigleProgressCallback)(const char* status, uint16_t progress, uint16_t total);

namespace WigleClient {
bool hasCredentials(const String& apiName, const String& apiToken);
bool loadUploadedList(fs::FS& fs, const char* uploadedPath);
uint16_t uploadedCount();
WigleUserStats getUserStats(fs::FS& fs, const char* statsPath);
const char* lastError();
bool isBusy();
void freeUploadedListMemory();
WigleSyncResult syncFiles(fs::FS& fs,
                          const char* rootDir,
                          const char* uploadedPath,
                          const char* statsPath,
                          const String& apiName,
                          const String& apiToken,
                          WigleProgressCallback cb = nullptr);
}

