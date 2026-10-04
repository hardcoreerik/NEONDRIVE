#pragma once
// G2 — Sub-GHz toolkit for the LilyGO T-Embed CC1101 Plus (B2).
// Clean-room implementation using the public RadioLib API (already a declared
// lib_dep for the t_embed env). Controlled-lab use only.
//
// Hardware (see src/neon_rf.cpp + main.cpp pin block):
//   CC1101: CS=12, GDO0=3, GDO2=38 on the shared HSPI bus (SCK=11/MISO=10/MOSI=9)
//   Antenna switch: SW1=47, SW0=48 (315: H/L, 434: H/H, 868/915: L/H)
//   PIN_PWR_EN=15 must be HIGH (done in setup()).
// The CC1101 shares HSPI with TFT/SD; all transfers use SPI transactions and
// only the CC1101 CS is ever asserted by this module.
//
// Evidence integration: startRx()/startScan() stage an AnalyzerSync SUBGHZ
// session; captures land in <session>/artifacts/ with wall-clock timestamps
// and events.csv rows. Nothing here runs on non-T-Embed targets (no-op stubs).
#include <Arduino.h>

namespace SubGhz {

// Supported bands (MHz)
constexpr float kBand315 = 315.0f;
constexpr float kBand434 = 434.0f;
constexpr float kBand868 = 868.0f;
constexpr float kBand915 = 915.0f;

enum class Mode : uint8_t { IDLE, SCAN, RX, TX, JAM };

struct Status {
    bool     present;      // radio detected on SPI
    Mode     mode;
    float    freqMhz;
    uint32_t captures;     // packets captured this RX run
    uint32_t txCount;      // replay/jam packets sent
    int16_t  lastRssi;     // dBm of last capture (or scan peak)
    float    peakFreqMhz;  // scan peak frequency
    char     lastError[64];
};

#if defined(NEONDRIVE_TARGET_T_EMBED_CC1101)

bool   available();            // SPI probe for the CC1101
bool   begin();                // init RadioLib on the shared bus (idempotent)
void   end();                  // standby + release
void   tick();                 // service RX flag / jam timing — call from loop()

bool   startScan(float f0Mhz, float f1Mhz, float stepMhz);  // RSSI sweep
bool   startRx(float freqMhz);                              // packet capture
bool   startReplay(float freqMhz, uint8_t times);           // replay last capture
bool   startJam(float freqMhz, uint32_t durationMs);        // noise burst jammer
void   stop();

Status status();
// Last scan table as JSON: [{"f":433.92,"rssi":-87}, ...] (truncated to fit)
String scanJson();
// Last captured packet as hex string (for the web UI)
String lastCaptureHex();

#else  // ── No-op stubs for every other target ──

inline bool   available()                 { return false; }
inline bool   begin()                     { return false; }
inline void   end()                       {}
inline void   tick()                      {}
inline bool   startScan(float, float, float) { return false; }
inline bool   startRx(float)              { return false; }
inline bool   startReplay(float, uint8_t) { return false; }
inline bool   startJam(float, uint32_t)   { return false; }
inline void   stop()                      {}
inline Status status() {
    Status s{}; s.present = false; s.mode = Mode::IDLE; return s;
}
inline String scanJson()       { return "[]"; }
inline String lastCaptureHex() { return ""; }

#endif
} // namespace SubGhz
