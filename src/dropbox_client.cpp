#include "dropbox_client.h"

#include <WiFiClientSecure.h>
#include <SD.h>

static constexpr const char* kHost = "content.dropboxapi.com";
static constexpr int          kPort = 443;
static constexpr const char* kPath = "/2/files/upload";

namespace DropboxClient {

UploadResult uploadFile(fs::FS& fs,
                        const char* sdPath,
                        const char* token,
                        const char* remotePathOverride) {
  UploadResult res;

  if (!token || token[0] == '\0') {
    strncpy(res.error, "NO TOKEN", sizeof(res.error) - 1);
    return res;
  }

  // Pass 1: get file size then close so the SD is free during TLS connect.
  // Holding a file open across a 15-second TLS handshake causes the SD SPI
  // to lose state on CYD, making subsequent f.read() calls return 0.
  {
    File probe = fs.open(sdPath, FILE_READ);
    if (!probe || probe.isDirectory()) {
      if (probe) probe.close();
      strncpy(res.error, "OPEN FAILED", sizeof(res.error) - 1);
      return res;
    }
    res.httpCode = 0;
    size_t sz = (size_t)probe.size();
    probe.close();
    // Store in a local we can use below.
    // (Re-declared as fileSize after the block.)
    (void)sz;
  }

  // Re-measure cleanly.
  size_t fileSize;
  {
    File probe = fs.open(sdPath, FILE_READ);
    if (!probe) { strncpy(res.error, "OPEN FAILED 2", sizeof(res.error)-1); return res; }
    fileSize = (size_t)probe.size();
    probe.close();
  }

  // Build remote path.
  char remotePath[128];
  if (remotePathOverride && remotePathOverride[0] != '\0') {
    snprintf(remotePath, sizeof(remotePath), "%s", remotePathOverride);
  } else {
    snprintf(remotePath, sizeof(remotePath), "%s", sdPath);
  }
  strncpy(res.remotePath, remotePath, sizeof(res.remotePath) - 1);

  // Escape remotePath for JSON.
  char jsonPath[140];
  {
    int j = 0;
    for (int i = 0; remotePath[i] && j < (int)sizeof(jsonPath) - 2; i++) {
      if (remotePath[i] == '"' || remotePath[i] == '\\') jsonPath[j++] = '\\';
      jsonPath[j++] = remotePath[i];
    }
    jsonPath[j] = '\0';
  }

  char apiArg[256];
  snprintf(apiArg, sizeof(apiArg),
           "{\"path\":\"%s\",\"mode\":\"overwrite\",\"autorename\":false,\"mute\":false}",
           jsonPath);

  // TLS connect — fresh object each call to avoid stale session state.
  WiFiClientSecure client;
  client.setInsecure();
  if (!client.connect(kHost, kPort, 15000)) {
    strncpy(res.error, "TLS CONNECT FAILED", sizeof(res.error) - 1);
    return res;
  }
  client.setTimeout(30000);

  client.printf("POST %s HTTP/1.1\r\n", kPath);
  client.printf("Host: %s\r\n", kHost);
  client.printf("Authorization: Bearer %s\r\n", token);
  client.printf("Dropbox-API-Arg: %s\r\n", apiArg);
  client.print("Content-Type: application/octet-stream\r\n");
  client.printf("Content-Length: %u\r\n", (unsigned int)fileSize);
  client.print("Connection: close\r\n\r\n");

  // Pass 2: reopen file now that TLS is ready — SD SPI is fresh.
  File f = fs.open(sdPath, FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    client.stop();
    strncpy(res.error, "REOPEN FAILED", sizeof(res.error) - 1);
    return res;
  }

  static uint8_t chunk[1024];
  size_t sent = 0;
  while (sent < fileSize) {
    size_t toRead = fileSize - sent;
    if (toRead > sizeof(chunk)) toRead = sizeof(chunk);
    size_t got = f.read(chunk, toRead);
    if (got == 0) break;
    if (client.write(chunk, got) != got) {
      f.close();
      client.stop();
      strncpy(res.error, "TLS WRITE FAILED", sizeof(res.error) - 1);
      return res;
    }
    sent += got;
    yield();
  }
  f.close();

  if (sent != fileSize) {
    client.stop();
    strncpy(res.error, "FILE READ SHORT", sizeof(res.error) - 1);
    return res;
  }

  client.flush();
  unsigned long deadline = millis() + 15000;
  while (client.connected() && !client.available() && millis() < deadline) {
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

  // Skip headers, grab first chunk of body for error detail.
  char body[96];
  body[0] = '\0';
  if (client.available()) {
    bool inBody = false;
    char hdrLine[80];
    unsigned long hdrDeadline = millis() + 5000;
    while (client.connected() && millis() < hdrDeadline) {
      size_t n = client.readBytesUntil('\n', hdrLine, sizeof(hdrLine) - 1);
      hdrLine[n] = '\0';
      if (n <= 1 || (n == 2 && hdrLine[0] == '\r')) { inBody = true; break; }
    }
    if (inBody && client.available()) {
      size_t n = client.readBytes(body, sizeof(body) - 1);
      body[n] = '\0';
    }
  }
  client.stop();

  res.httpCode = statusCode;
  res.ok = (statusCode == 200);
  if (!res.ok) {
    if (body[0])
      snprintf(res.error, sizeof(res.error), "HTTP %d: %s", statusCode, body);
    else
      snprintf(res.error, sizeof(res.error), "HTTP %d", statusCode);
    Serial.printf("[dropbox] error body: %s\n", body);
  }
  return res;
}

} // namespace DropboxClient
