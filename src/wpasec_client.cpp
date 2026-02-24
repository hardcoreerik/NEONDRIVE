#include "wpasec_client.h"

#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <vector>

namespace {

static constexpr const char* kHost = "wpa-sec.stanev.org";
static constexpr uint16_t kPort = 443;
static constexpr const char* kUploadPath = "/";
static constexpr const char* kPotfilePath = "/?api&dl=1";
static constexpr size_t kMaxEntries = 600;
static constexpr size_t kMaxPendingUploads = 32;

struct CrackedEntry {
  char bssid[13];
  char ssid[33];
  char password[65];
};

struct UploadedEntry {
  char bssid[13];
};

struct PendingCapture {
  char path[192];
  char bssid[13];
};

static std::vector<CrackedEntry> gCracked;
static std::vector<UploadedEntry> gUploaded;
static bool gCacheLoaded = false;
static volatile bool gBusy = false;
static char gLastError[64] = {0};

static void setLastError(const char* msg) {
  if (!msg) msg = "";
  strncpy(gLastError, msg, sizeof(gLastError) - 1);
  gLastError[sizeof(gLastError) - 1] = '\0';
}

static bool isHexChar(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static char toHexUpper(char c) {
  if (c >= 'a' && c <= 'f') return (char)(c - 32);
  return c;
}

static void normalizeBssid(const char* in, char out[13]) {
  if (!in) {
    out[0] = '\0';
    return;
  }
  size_t j = 0;
  for (size_t i = 0; in[i] && j < 12; i++) {
    char c = in[i];
    if (c == ':' || c == '-') continue;
    if (!isHexChar(c)) continue;
    out[j++] = toHexUpper(c);
  }
  out[j] = '\0';
  if (j != 12) out[0] = '\0';
}

static const CrackedEntry* findCracked(const char* normalizedBssid) {
  for (size_t i = 0; i < gCracked.size(); i++) {
    if (strcmp(gCracked[i].bssid, normalizedBssid) == 0) return &gCracked[i];
  }
  return nullptr;
}

static bool isBssidUploaded(const char* normalizedBssid) {
  for (size_t i = 0; i < gUploaded.size(); i++) {
    if (strcmp(gUploaded[i].bssid, normalizedBssid) == 0) return true;
  }
  return false;
}

static bool saveUploadedList(fs::FS& fs, const char* uploadedPath) {
  if (!uploadedPath || uploadedPath[0] == '\0') return false;
  File f = fs.open(uploadedPath, "w");
  if (!f) {
    setLastError("CANNOT WRITE UPLOADED");
    return false;
  }
  for (size_t i = 0; i < gUploaded.size(); i++) {
    f.println(gUploaded[i].bssid);
  }
  f.close();
  return true;
}

static bool loadUploadedList(fs::FS& fs, const char* uploadedPath) {
  gUploaded.clear();
  gUploaded.reserve(96);

  if (!uploadedPath || uploadedPath[0] == '\0') return true;
  if (!fs.exists(uploadedPath)) return true;

  File f = fs.open(uploadedPath, "r");
  if (!f) {
    setLastError("CANNOT OPEN UPLOADED");
    return false;
  }

  char line[64];
  while (f.available() && gUploaded.size() < kMaxEntries) {
    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) {
      line[--n] = '\0';
    }
    char bssid[13];
    normalizeBssid(line, bssid);
    if (bssid[0] == '\0') continue;
    if (isBssidUploaded(bssid)) continue;
    UploadedEntry e{};
    memcpy(e.bssid, bssid, sizeof(e.bssid));
    gUploaded.push_back(e);
  }
  f.close();
  return true;
}

static bool loadCrackedCache(fs::FS& fs, const char* resultsPath) {
  gCracked.clear();
  gCracked.reserve(128);

  if (!resultsPath || resultsPath[0] == '\0') return true;
  if (!fs.exists(resultsPath)) return true;

  File f = fs.open(resultsPath, "r");
  if (!f) {
    setLastError("CANNOT OPEN CACHE");
    return false;
  }

  char line[192];
  while (f.available() && gCracked.size() < kMaxEntries) {
    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) {
      line[--n] = '\0';
    }
    if (n < 20) continue;

    const char* c1 = nullptr;
    const char* c2 = nullptr;
    const char* c3 = nullptr;
    for (const char* p = line; *p; p++) {
      if (*p == ':') {
        if (!c1) c1 = p;
        else if (!c2) c2 = p;
        else {
          c3 = p;
          break;
        }
      }
    }
    if (!c1 || !c2 || !c3) continue;
    if ((c1 - line) != 12 || (c2 - line) != 25) continue;

    char apBssid[13];
    memcpy(apBssid, line, 12);
    apBssid[12] = '\0';

    CrackedEntry e{};
    normalizeBssid(apBssid, e.bssid);
    if (e.bssid[0] == '\0') continue;

    size_t ssidLen = (size_t)(c3 - c2 - 1);
    if (ssidLen >= sizeof(e.ssid)) ssidLen = sizeof(e.ssid) - 1;
    memcpy(e.ssid, c2 + 1, ssidLen);
    e.ssid[ssidLen] = '\0';

    const char* pw = c3 + 1;
    size_t pwLen = strlen(pw);
    if (pwLen >= sizeof(e.password)) pwLen = sizeof(e.password) - 1;
    memcpy(e.password, pw, pwLen);
    e.password[pwLen] = '\0';

    gCracked.push_back(e);
  }

  f.close();
  return true;
}

static bool endsWith(const char* s, const char* suffix) {
  if (!s || !suffix) return false;
  size_t ls = strlen(s);
  size_t le = strlen(suffix);
  if (ls < le) return false;
  return strcmp(s + (ls - le), suffix) == 0;
}

static bool isCaptureCandidate(const char* path) {
  if (!path) return false;
  if (endsWith(path, ".22000")) return true;
  if (endsWith(path, "/handshakes.pcap")) return true;
  if (endsWith(path, "_hs.pcap")) return true;
  return false;
}

static bool parseBssidFromDirectoryName(const char* dirName, char out[13]) {
  if (!dirName) return false;
  size_t n = strlen(dirName);
  if (n < 17) return false;

  // Format used by this project: SSID_AA-BB-CC-DD-EE-FF
  const char* p = dirName + n - 17;
  for (int i = 0; i < 17; i++) {
    char c = p[i];
    if ((i % 3) == 2) {
      if (c != '-') return false;
    } else {
      if (!isHexChar(c)) return false;
    }
  }

  char raw[18];
  memcpy(raw, p, 17);
  raw[17] = '\0';
  normalizeBssid(raw, out);
  return out[0] != '\0';
}

static bool parseBssidFromFilename(const char* fileName, char out[13]) {
  if (!fileName) return false;
  size_t n = strlen(fileName);
  if (n < 12) return false;

  // Legacy format: BSSID12HEX.ext
  bool first12Hex = true;
  for (int i = 0; i < 12; i++) {
    if (!isHexChar(fileName[i])) {
      first12Hex = false;
      break;
    }
  }
  if (first12Hex) {
    char raw[13];
    memcpy(raw, fileName, 12);
    raw[12] = '\0';
    normalizeBssid(raw, out);
    if (out[0]) return true;
  }

  // New format: SSID_BSSID12HEX_hs.pcap or SSID_BSSID12HEX.22000
  const char* dot = strrchr(fileName, '.');
  size_t baseLen = dot ? (size_t)(dot - fileName) : n;
  if (baseLen > 3 && strncmp(fileName + baseLen - 3, "_hs", 3) == 0) baseLen -= 3;

  if (baseLen >= 12) {
    size_t pos = baseLen - 12;
    bool last12Hex = true;
    for (size_t i = pos; i < pos + 12; i++) {
      if (!isHexChar(fileName[i])) {
        last12Hex = false;
        break;
      }
    }
    if (last12Hex) {
      char raw[13];
      memcpy(raw, fileName + pos, 12);
      raw[12] = '\0';
      normalizeBssid(raw, out);
      if (out[0]) return true;
    }
  }

  return false;
}

static bool parseBssidFromPath(const char* path, char out[13]) {
  out[0] = '\0';
  if (!path) return false;

  // Try parent directory first.
  const char* lastSlash = strrchr(path, '/');
  if (lastSlash && lastSlash > path) {
    const char* prevSlash = lastSlash - 1;
    while (prevSlash > path && *prevSlash != '/') prevSlash--;
    const char* dirStart = (prevSlash == path && *prevSlash == '/') ? (prevSlash + 1) : (prevSlash + 1);
    size_t dirLen = (size_t)(lastSlash - dirStart);
    if (dirLen > 0 && dirLen < 80) {
      char dirName[80];
      memcpy(dirName, dirStart, dirLen);
      dirName[dirLen] = '\0';
      if (parseBssidFromDirectoryName(dirName, out)) return true;
    }
  }

  const char* fname = lastSlash ? (lastSlash + 1) : path;
  return parseBssidFromFilename(fname, out);
}

static void collectCaptureFilesRec(fs::FS& fs,
                                   const char* dirPath,
                                   int depth,
                                   PendingCapture* out,
                                   size_t outCap,
                                   size_t& outCount,
                                   uint16_t& skipped) {
  if (!dirPath || outCount >= outCap || depth > 8) return;

  File dir = fs.open(dirPath, "r");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  while (outCount < outCap) {
    File entry = dir.openNextFile();
    if (!entry) break;
    String name = String(entry.name());
    String full = name;
    if (!name.startsWith("/")) {
      full = String(dirPath);
      if (!full.endsWith("/")) full += "/";
      full += name;
    }
    bool isDir = entry.isDirectory();
    entry.close();
    yield();

    if (isDir) {
      collectCaptureFilesRec(fs, full.c_str(), depth + 1, out, outCap, outCount, skipped);
      continue;
    }

    if (!isCaptureCandidate(full.c_str())) continue;

    char bssid[13];
    if (!parseBssidFromPath(full.c_str(), bssid)) continue;
    if (findCracked(bssid) || isBssidUploaded(bssid)) {
      if (skipped < 65535) skipped++;
      continue;
    }

    strncpy(out[outCount].path, full.c_str(), sizeof(out[outCount].path) - 1);
    out[outCount].path[sizeof(out[outCount].path) - 1] = '\0';
    memcpy(out[outCount].bssid, bssid, sizeof(out[outCount].bssid));
    outCount++;
    yield();
  }

  dir.close();
}

static bool uploadSingleCapture(fs::FS& fs, const char* path, const char* apiKey) {
  File cap = fs.open(path, "r");
  if (!cap || cap.isDirectory()) {
    if (cap) cap.close();
    setLastError("OPEN CAPTURE FAILED");
    return false;
  }

  size_t fileSize = cap.size();
  if (fileSize == 0 || fileSize > (1024 * 1024)) {
    cap.close();
    setLastError("CAPTURE SIZE INVALID");
    return false;
  }

  const char* filename = strrchr(path, '/');
  filename = filename ? filename + 1 : path;

  static WiFiClientSecure client;
  client.stop();
  client.setInsecure();
  if (!client.connect(kHost, kPort, 15000)) {
    cap.close();
    setLastError("TLS CONNECT FAILED");
    return false;
  }
  client.setTimeout(30000);

  static char boundary[40];
  snprintf(boundary, sizeof(boundary), "----WPASEC%08lX", millis());

  static char bodyHead[240];
  int headLen = snprintf(bodyHead, sizeof(bodyHead),
                         "--%s\r\n"
                         "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
                         "Content-Type: application/octet-stream\r\n\r\n",
                         boundary, filename);
  static char bodyTail[64];
  int tailLen = snprintf(bodyTail, sizeof(bodyTail), "\r\n--%s--\r\n", boundary);
  size_t contentLen = (size_t)headLen + fileSize + (size_t)tailLen;

  client.printf("POST %s HTTP/1.1\r\n", kUploadPath);
  client.printf("Host: %s\r\n", kHost);
  client.printf("Cookie: key=%s\r\n", apiKey);
  client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary);
  client.printf("Content-Length: %u\r\n", (unsigned int)contentLen);
  client.print("Connection: close\r\n\r\n");
  client.print(bodyHead);

  static uint8_t chunk[1024];
  size_t sent = 0;
  while (sent < fileSize) {
    size_t toRead = fileSize - sent;
    if (toRead > sizeof(chunk)) toRead = sizeof(chunk);
    size_t got = cap.read(chunk, toRead);
    if (got == 0) break;
    size_t wr = client.write(chunk, got);
    if (wr != got) {
      cap.close();
      client.stop();
      setLastError("TLS WRITE FAILED");
      return false;
    }
    sent += got;
    yield();
  }
  cap.close();

  if (sent != fileSize) {
    client.stop();
    setLastError("CAPTURE READ FAILED");
    return false;
  }

  client.print(bodyTail);
  client.flush();

  unsigned long timeoutAt = millis() + 15000;
  while (client.connected() && !client.available() && millis() < timeoutAt) {
    delay(5);
    yield();
  }

  int statusCode = 0;
  if (client.available()) {
    static char statusLine[96];
    size_t n = client.readBytesUntil('\n', statusLine, sizeof(statusLine) - 1);
    statusLine[n] = '\0';
    const char* sp = strchr(statusLine, ' ');
    if (sp) statusCode = atoi(sp + 1);
  }

  client.stop();

  if (statusCode == 200 || statusCode == 201 || statusCode == 202 || statusCode == 409) {
    return true;
  }

  setLastError("UPLOAD REJECTED");
  return false;
}

static bool downloadPotfile(fs::FS& fs, const char* resultsPath, const char* apiKey, uint16_t& linesWritten) {
  linesWritten = 0;

  static WiFiClientSecure client;
  client.stop();
  client.setInsecure();
  if (!client.connect(kHost, kPort, 15000)) {
    setLastError("POTFILE TLS FAILED");
    return false;
  }
  client.setTimeout(30000);

  client.printf("GET %s HTTP/1.1\r\n", kPotfilePath);
  client.printf("Host: %s\r\n", kHost);
  client.printf("Cookie: key=%s\r\n", apiKey);
  client.print("Connection: close\r\n\r\n");

  unsigned long timeoutAt = millis() + 20000;
  while (client.connected() && !client.available() && millis() < timeoutAt) {
    delay(5);
    yield();
  }
  if (!client.available()) {
    client.stop();
    setLastError("POTFILE TIMEOUT");
    return false;
  }

  int statusCode = 0;
  static char line[192];
  size_t n = client.readBytesUntil('\n', line, sizeof(line) - 1);
  line[n] = '\0';
  const char* sp = strchr(line, ' ');
  if (sp) statusCode = atoi(sp + 1);
  if (statusCode != 200) {
    client.stop();
    setLastError("POTFILE HTTP ERROR");
    return false;
  }

  bool headersDone = false;
  while (client.connected() && !headersDone) {
    size_t hn = client.readBytesUntil('\n', line, sizeof(line) - 1);
    line[hn] = '\0';
    if (hn <= 1 || (hn == 2 && line[0] == '\r')) headersDone = true;
  }

  if (fs.exists(resultsPath)) fs.remove(resultsPath);
  File out = fs.open(resultsPath, "w");
  if (!out) {
    client.stop();
    setLastError("CANNOT WRITE POTFILE");
    return false;
  }

  while (client.connected() || client.available()) {
    if (!client.available()) {
      delay(2);
      yield();
      continue;
    }
    size_t rn = client.readBytesUntil('\n', line, sizeof(line) - 1);
    line[rn] = '\0';
    while (rn > 0 && (line[rn - 1] == '\r' || line[rn - 1] == ' ' || line[rn - 1] == '\t')) {
      line[--rn] = '\0';
    }
    if (rn == 0) continue;

    int colonCount = 0;
    for (size_t i = 0; line[i]; i++) {
      if (line[i] == ':') colonCount++;
    }
    if (colonCount >= 3) {
      out.println(line);
      if (linesWritten < 65535) linesWritten++;
    }
    yield();
  }

  out.close();
  client.stop();
  return true;
}

static void markUploaded(const char* normalizedBssid) {
  if (!normalizedBssid || normalizedBssid[0] == '\0') return;
  if (isBssidUploaded(normalizedBssid)) return;
  if (gUploaded.size() >= kMaxEntries) return;
  UploadedEntry e{};
  memcpy(e.bssid, normalizedBssid, sizeof(e.bssid));
  gUploaded.push_back(e);
}

}  // namespace

namespace WPASecClient {

bool hasApiKey(const String& key) {
  if (key.length() != 32) return false;
  for (size_t i = 0; i < key.length(); i++) {
    if (!isHexChar(key[i])) return false;
  }
  return true;
}

bool loadCache(fs::FS& fs, const char* resultsPath, const char* uploadedPath) {
  if (gCacheLoaded) return true;
  if (!loadCrackedCache(fs, resultsPath)) return false;
  if (!loadUploadedList(fs, uploadedPath)) return false;
  gCacheLoaded = true;
  return true;
}

bool isCracked(const char* bssid) {
  char key[13];
  normalizeBssid(bssid, key);
  if (key[0] == '\0') return false;
  return findCracked(key) != nullptr;
}

const char* getPassword(const char* bssid) {
  char key[13];
  normalizeBssid(bssid, key);
  if (key[0] == '\0') return "";
  const CrackedEntry* e = findCracked(key);
  return e ? e->password : "";
}

const char* getSsid(const char* bssid) {
  char key[13];
  normalizeBssid(bssid, key);
  if (key[0] == '\0') return "";
  const CrackedEntry* e = findCracked(key);
  return e ? e->ssid : "";
}

uint16_t crackedCount() {
  return (uint16_t)gCracked.size();
}

const char* lastError() {
  return gLastError;
}

bool isBusy() {
  return gBusy;
}

void freeCacheMemory() {
  gCracked.clear();
  gCracked.shrink_to_fit();
  gUploaded.clear();
  gUploaded.shrink_to_fit();
  gCacheLoaded = false;
}

WPASecSyncResult syncCaptures(fs::FS& fs,
                              const char* capturesRoot,
                              const char* uploadedPath,
                              const char* resultsPath,
                              const String& apiKey,
                              WPASecProgressCallback cb) {
  WPASecSyncResult out;
  gBusy = true;
  setLastError("");

  if (!capturesRoot || capturesRoot[0] == '\0') {
    strncpy(out.error, "NO CAPTURE ROOT", sizeof(out.error) - 1);
    gBusy = false;
    return out;
  }

  if (!hasApiKey(apiKey)) {
    strncpy(out.error, "NO WPA-SEC KEY", sizeof(out.error) - 1);
    gBusy = false;
    return out;
  }

  if (WiFi.status() != WL_CONNECTED) {
    strncpy(out.error, "WIFI NOT CONNECTED", sizeof(out.error) - 1);
    gBusy = false;
    return out;
  }

  gCacheLoaded = false;
  if (!loadCache(fs, resultsPath, uploadedPath)) {
    strncpy(out.error, lastError(), sizeof(out.error) - 1);
    gBusy = false;
    return out;
  }

  static PendingCapture pending[kMaxPendingUploads];
  size_t pendingCount = 0;
  uint16_t skipped = 0;
  if (cb) cb("scan captures", 0, 0);
  collectCaptureFilesRec(fs, capturesRoot, 0, pending, kMaxPendingUploads, pendingCount, skipped);

  if (cb) cb("uploading", 0, (uint16_t)pendingCount);
  for (size_t i = 0; i < pendingCount; i++) {
    if (cb) cb("upload", (uint16_t)(i + 1), (uint16_t)pendingCount);
    if (uploadSingleCapture(fs, pending[i].path, apiKey.c_str())) {
      out.uploaded++;
      markUploaded(pending[i].bssid);
    } else {
      out.failed++;
      Serial.printf("[wpasec] upload failed: %s (%s)\n", pending[i].path, lastError());
    }
    delay(50);
    yield();
  }

  out.skipped = skipped;

  if (!saveUploadedList(fs, uploadedPath)) {
    Serial.printf("[wpasec] save uploaded list failed: %s\n", lastError());
  }

  if (cb) cb("download potfile", 0, 0);
  uint16_t newLines = 0;
  if (downloadPotfile(fs, resultsPath, apiKey.c_str(), newLines)) {
    out.newCracked = newLines;
    gCacheLoaded = false;
    if (loadCache(fs, resultsPath, uploadedPath)) {
      out.cracked = crackedCount();
    }
  } else {
    Serial.printf("[wpasec] potfile failed: %s\n", lastError());
  }

  if (out.failed == 0) {
    out.success = true;
  } else if (out.uploaded > 0) {
    out.success = true;
    strncpy(out.error, "PARTIAL", sizeof(out.error) - 1);
  } else {
    out.success = false;
    strncpy(out.error, lastError(), sizeof(out.error) - 1);
  }

  gBusy = false;
  return out;
}

}  // namespace WPASecClient
