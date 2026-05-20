#pragma once

#include <Arduino.h>
#include <FS.h>

namespace DropboxClient {

struct UploadResult {
  bool    ok       = false;
  int     httpCode = 0;
  char    error[96] = {};
  char    remotePath[128] = {};
};

// Upload a file from any FS (SD/LittleFS) to Dropbox.
// sdPath is the source path on the FS (e.g. "/captures/net/file.22000").
// remotePath is the destination in Dropbox (e.g. "/NEONDRIVE/captures/net/file.22000").
// If remotePath is empty, mirrors sdPath under /NEONDRIVE/.
UploadResult uploadFile(fs::FS& fs,
                        const char* sdPath,
                        const char* token,
                        const char* remotePathOverride = nullptr);

} // namespace DropboxClient
