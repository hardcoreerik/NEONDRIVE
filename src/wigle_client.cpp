#include "wigle_client.h"

#include <ArduinoJson.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include <esp_heap_caps.h>
#include <vector>

namespace {

static constexpr const char* kHost = "api.wigle.net";
static constexpr uint16_t kPort = 443;
static constexpr const char* kUploadPath = "/api/v2/file/upload";
static constexpr const char* kStatsPath = "/api/v2/stats/user";
static constexpr size_t kMaxTracked = 400;
static constexpr size_t kMaxPendingUploads = 32;

struct UploadedEntry {
  char name[64];
};

struct PendingCsv {
  char path[192];
  char name[64];
};

static std::vector<UploadedEntry> gUploaded;
static bool gLoaded = false;
static volatile bool gBusy = false;
static char gLastError[64] = {0};

static void setLastError(const char* msg) {
  if (!msg) msg = "";
  strncpy(gLastError, msg, sizeof(gLastError) - 1);
  gLastError[sizeof(gLastError) - 1] = '\0';
}

static bool endsWith(const char* s, const char* suffix) {
  if (!s || !suffix) return false;
  size_t ls = strlen(s);
  size_t le = strlen(suffix);
  if (ls < le) return false;
  return strcmp(s + (ls - le), suffix) == 0;
}

static const char* baseName(const char* path) {
  if (!path) return "";
  const char* s = strrchr(path, '/');
  return s ? (s + 1) : path;
}

static bool isTracked(const char* nameOrPath) {
  if (!nameOrPath) return false;
  const char* n = baseName(nameOrPath);
  for (size_t i = 0; i < gUploaded.size(); i++) {
    if (strcmp(gUploaded[i].name, nameOrPath) == 0 || strcmp(gUploaded[i].name, n) == 0) return true;
  }
  return false;
}

static bool saveUploadedList(fs::FS& fs, const char* uploadedPath) {
  if (!uploadedPath || uploadedPath[0] == '\0') return false;
  File f = fs.open(uploadedPath, "w");
  if (!f) {
    setLastError("CANNOT WRITE WIGLE UPLOADED");
    return false;
  }
  for (size_t i = 0; i < gUploaded.size(); i++) {
    f.println(gUploaded[i].name);
  }
  f.close();
  return true;
}

static bool trackUploaded(const char* nameOrPath) {
  const char* n = baseName(nameOrPath);
  if (!n || n[0] == '\0') return false;
  if (isTracked(n)) return true;
  if (gUploaded.size() >= kMaxTracked) return false;
  UploadedEntry e{};
  strncpy(e.name, n, sizeof(e.name) - 1);
  e.name[sizeof(e.name) - 1] = '\0';
  gUploaded.push_back(e);
  return true;
}

static void collectWigleCsvRec(fs::FS& fs,
                               const char* dirPath,
                               int depth,
                               PendingCsv* out,
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
      collectWigleCsvRec(fs, full.c_str(), depth + 1, out, outCap, outCount, skipped);
      continue;
    }

    if (!endsWith(full.c_str(), ".wigle.csv")) continue;

    const char* n = baseName(full.c_str());
    if (isTracked(n)) {
      skipped++;
      continue;
    }

    strncpy(out[outCount].path, full.c_str(), sizeof(out[outCount].path) - 1);
    out[outCount].path[sizeof(out[outCount].path) - 1] = '\0';
    strncpy(out[outCount].name, n, sizeof(out[outCount].name) - 1);
    out[outCount].name[sizeof(out[outCount].name) - 1] = '\0';
    outCount++;
    yield();
  }

  dir.close();
}

static bool buildBasicAuth(const String& name, const String& token, char* out, size_t outLen) {
  if (!out || outLen < 16) return false;
  static char creds[196];
  snprintf(creds, sizeof(creds), "%s:%s", name.c_str(), token.c_str());
  static char b64[280];
  size_t olen = 0;
  int rc = mbedtls_base64_encode((unsigned char*)b64, sizeof(b64), &olen,
                                 (const unsigned char*)creds, strlen(creds));
  if (rc != 0 || olen == 0) return false;
  b64[olen] = '\0';
  snprintf(out, outLen, "Basic %s", b64);
  return true;
}

static bool uploadSingleFile(fs::FS& fs, const char* path, const char* authHeader) {
  File csv = fs.open(path, "r");
  if (!csv || csv.isDirectory()) {
    if (csv) csv.close();
    setLastError("OPEN CSV FAILED");
    return false;
  }

  size_t fileSize = csv.size();
  if (fileSize == 0 || fileSize > (2 * 1024 * 1024)) {
    csv.close();
    setLastError("CSV SIZE INVALID");
    return false;
  }

  const char* filename = baseName(path);

  static WiFiClientSecure client;
  client.stop();
  client.setInsecure();
  if (!client.connect(kHost, kPort, 15000)) {
    csv.close();
    setLastError("WIGLE TLS CONNECT FAILED");
    return false;
  }
  client.setTimeout(30000);

  static char boundary[48];
  snprintf(boundary, sizeof(boundary), "----WIGLE%08lX", millis());

  static char bodyHead[260];
  int headLen = snprintf(bodyHead, sizeof(bodyHead),
                         "--%s\r\n"
                         "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
                         "Content-Type: text/csv\r\n\r\n",
                         boundary, filename);
  static char bodyTail[72];
  int tailLen = snprintf(bodyTail, sizeof(bodyTail), "\r\n--%s--\r\n", boundary);
  size_t contentLen = (size_t)headLen + fileSize + (size_t)tailLen;

  client.printf("POST %s HTTP/1.1\r\n", kUploadPath);
  client.printf("Host: %s\r\n", kHost);
  client.print("Authorization: ");
  client.print(authHeader);
  client.print("\r\n");
  client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary);
  client.printf("Content-Length: %u\r\n", (unsigned int)contentLen);
  client.print("Connection: close\r\n\r\n");
  client.print(bodyHead);

  static uint8_t chunk[1536];
  size_t sent = 0;
  while (sent < fileSize) {
    size_t toRead = fileSize - sent;
    if (toRead > sizeof(chunk)) toRead = sizeof(chunk);
    size_t got = csv.read(chunk, toRead);
    if (got == 0) break;
    size_t wr = client.write(chunk, got);
    if (wr != got) {
      csv.close();
      client.stop();
      setLastError("WIGLE TLS WRITE FAILED");
      return false;
    }
    sent += got;
    yield();
  }
  csv.close();

  if (sent != fileSize) {
    client.stop();
    setLastError("WIGLE CSV READ FAILED");
    return false;
  }

  client.print(bodyTail);
  client.flush();

  unsigned long timeoutAt = millis() + 20000;
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

  if (statusCode == 200 || statusCode == 201 || statusCode == 202 || statusCode == 302) {
    return true;
  }

  setLastError("WIGLE HTTP ERROR");
  return false;
}

static bool fetchStats(fs::FS& fs, const char* statsPath, const char* authHeader) {
  static WiFiClientSecure client;
  client.stop();
  client.setInsecure();
  if (!client.connect(kHost, kPort, 15000)) {
    setLastError("WIGLE STATS TLS FAILED");
    return false;
  }
  client.setTimeout(30000);

  client.printf("GET %s HTTP/1.1\r\n", kStatsPath);
  client.printf("Host: %s\r\n", kHost);
  client.print("Authorization: ");
  client.print(authHeader);
  client.print("\r\n");
  client.print("Connection: close\r\n\r\n");

  unsigned long timeoutAt = millis() + 20000;
  while (client.connected() && !client.available() && millis() < timeoutAt) {
    delay(5);
    yield();
  }
  if (!client.available()) {
    client.stop();
    setLastError("WIGLE STATS TIMEOUT");
    return false;
  }

  int statusCode = 0;
  static char line[160];
  size_t n = client.readBytesUntil('\n', line, sizeof(line) - 1);
  line[n] = '\0';
  const char* sp = strchr(line, ' ');
  if (sp) statusCode = atoi(sp + 1);
  if (statusCode != 200) {
    client.stop();
    setLastError("WIGLE STATS HTTP ERROR");
    return false;
  }

  bool headersDone = false;
  while (client.connected() && !headersDone) {
    size_t hn = client.readBytesUntil('\n', line, sizeof(line) - 1);
    line[hn] = '\0';
    if (hn <= 1 || (hn == 2 && line[0] == '\r')) headersDone = true;
  }

  static char body[3072];
  size_t bodyLen = 0;
  unsigned long bodyTimeout = millis() + 10000;
  while ((client.connected() || client.available()) && bodyLen < sizeof(body) - 1 && millis() < bodyTimeout) {
    if (client.available()) {
      body[bodyLen++] = (char)client.read();
    } else {
      delay(1);
    }
  }
  body[bodyLen] = '\0';
  client.stop();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, bodyLen);
  if (err) {
    setLastError("WIGLE STATS JSON ERROR");
    return false;
  }

  JsonDocument outDoc;
  outDoc["rank"] = doc["rank"] | (doc["statistics"]["rank"] | 0);
  JsonObject stats = doc["statistics"].as<JsonObject>();
  if (stats) {
    outDoc["wifi"] = stats["discoveredWiFi"] | stats["wifiCount"] | 0;
    outDoc["cell"] = stats["discoveredCell"] | stats["cellCount"] | 0;
    outDoc["bt"] = stats["discoveredBt"] | stats["btCount"] | 0;
  } else {
    outDoc["wifi"] = 0;
    outDoc["cell"] = 0;
    outDoc["bt"] = 0;
  }

  if (fs.exists(statsPath)) fs.remove(statsPath);
  File out = fs.open(statsPath, "w");
  if (!out) {
    setLastError("WRITE WIGLE STATS FAILED");
    return false;
  }
  serializeJson(outDoc, out);
  out.close();
  return true;
}

}  // namespace

namespace WigleClient {

bool hasCredentials(const String& apiName, const String& apiToken) {
  return apiName.length() > 0 && apiToken.length() > 0;
}

bool loadUploadedList(fs::FS& fs, const char* uploadedPath) {
  if (gLoaded) return true;

  gUploaded.clear();
  gUploaded.reserve(96);
  if (!uploadedPath || uploadedPath[0] == '\0') {
    gLoaded = true;
    return true;
  }
  if (!fs.exists(uploadedPath)) {
    gLoaded = true;
    return true;
  }

  File f = fs.open(uploadedPath, "r");
  if (!f) {
    setLastError("OPEN WIGLE UPLOADED FAILED");
    return false;
  }

  char line[80];
  while (f.available() && gUploaded.size() < kMaxTracked) {
    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) {
      line[--n] = '\0';
    }
    if (n == 0) continue;
    if (isTracked(line)) continue;
    UploadedEntry e{};
    strncpy(e.name, line, sizeof(e.name) - 1);
    e.name[sizeof(e.name) - 1] = '\0';
    gUploaded.push_back(e);
  }

  f.close();
  gLoaded = true;
  return true;
}

uint16_t uploadedCount() {
  return (uint16_t)gUploaded.size();
}

WigleUserStats getUserStats(fs::FS& fs, const char* statsPath) {
  WigleUserStats s;
  if (!statsPath || statsPath[0] == '\0') return s;
  if (!fs.exists(statsPath)) return s;

  File f = fs.open(statsPath, "r");
  if (!f) return s;

  size_t size = f.size();
  if (size == 0 || size > 1024) {
    f.close();
    return s;
  }

  char buf[1024];
  size_t n = f.readBytes(buf, sizeof(buf) - 1);
  buf[n] = '\0';
  f.close();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, buf, n);
  if (err) return s;

  s.rank = doc["rank"] | 0;
  s.wifi = doc["wifi"] | 0;
  s.cell = doc["cell"] | 0;
  s.bt = doc["bt"] | 0;
  s.valid = true;
  return s;
}

const char* lastError() {
  return gLastError;
}

bool isBusy() {
  return gBusy;
}

void freeUploadedListMemory() {
  gUploaded.clear();
  gUploaded.shrink_to_fit();
  gLoaded = false;
}

WigleSyncResult syncFiles(fs::FS& fs,
                          const char* rootDir,
                          const char* uploadedPath,
                          const char* statsPath,
                          const String& apiName,
                          const String& apiToken,
                          WigleProgressCallback cb) {
  WigleSyncResult out;
  gBusy = true;
  setLastError("");

  if (!rootDir || rootDir[0] == '\0') {
    strncpy(out.error, "NO WARDRIVING ROOT", sizeof(out.error) - 1);
    gBusy = false;
    return out;
  }
  if (!hasCredentials(apiName, apiToken)) {
    strncpy(out.error, "NO WIGLE CREDENTIALS", sizeof(out.error) - 1);
    gBusy = false;
    return out;
  }
  if (WiFi.status() != WL_CONNECTED) {
    strncpy(out.error, "WIFI NOT CONNECTED", sizeof(out.error) - 1);
    gBusy = false;
    return out;
  }

  gLoaded = false;
  if (!loadUploadedList(fs, uploadedPath)) {
    strncpy(out.error, lastError(), sizeof(out.error) - 1);
    gBusy = false;
    return out;
  }

  static PendingCsv pending[kMaxPendingUploads];
  size_t pendingCount = 0;
  uint16_t skipped = 0;
  if (cb) cb("scan wigle csv", 0, 0);
  collectWigleCsvRec(fs, rootDir, 0, pending, kMaxPendingUploads, pendingCount, skipped);
  out.skipped = skipped;

  static char authHeader[320];
  if (!buildBasicAuth(apiName, apiToken, authHeader, sizeof(authHeader))) {
    strncpy(out.error, "AUTH HEADER FAILED", sizeof(out.error) - 1);
    gBusy = false;
    return out;
  }

  if (cb) cb("upload wigle", 0, (uint16_t)pendingCount);
  for (size_t i = 0; i < pendingCount; i++) {
    if (cb) cb("upload", (uint16_t)(i + 1), (uint16_t)pendingCount);
    if (uploadSingleFile(fs, pending[i].path, authHeader)) {
      out.uploaded++;
      trackUploaded(pending[i].name);
    } else {
      out.failed++;
      Serial.printf("[wigle] upload failed: %s (%s)\n", pending[i].path, lastError());
    }
    delay(50);
    yield();
  }

  if (!saveUploadedList(fs, uploadedPath)) {
    Serial.printf("[wigle] save uploaded list failed: %s\n", lastError());
  }

  size_t freeHeap = ESP.getFreeHeap();
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (freeHeap < 70000 || largest < 45000) {
    out.statsFetched = false;
    setLastError("WIGLE STATS SKIPPED LOW HEAP");
    Serial.printf("[wigle] stats skipped: free=%u largest=%u\n",
                  (unsigned)freeHeap, (unsigned)largest);
  } else {
    if (cb) cb("fetch stats", 0, 0);
    out.statsFetched = fetchStats(fs, statsPath, authHeader);
    if (!out.statsFetched) {
      Serial.printf("[wigle] stats fetch failed: %s\n", lastError());
    }
  }

  if (out.failed == 0 || out.uploaded > 0 || pendingCount == 0) {
    out.success = true;
    if (out.failed > 0) strncpy(out.error, "PARTIAL", sizeof(out.error) - 1);
  } else {
    out.success = false;
    strncpy(out.error, lastError(), sizeof(out.error) - 1);
  }

  gBusy = false;
  return out;
}

}  // namespace WigleClient
