// G2 — Sub-GHz toolkit for the LilyGO T-Embed CC1101 Plus (B2).
// Clean-room implementation using the public RadioLib API (MIT-licensed
// library, already a declared lib_dep for the t_embed env).
//
// Shared-bus discipline: the CC1101 lives on HSPI (SCK=11/MISO=10/MOSI=9)
// with TFT and SD. We begin() our own SPIClass on the same pins once; the
// SPI-parameterized RadioLib Module constructor does NOT re-begin the bus,
// so TFT/SD pin config is preserved. All transfers use SPI transactions and
// only CS=12 is ever asserted here.
#include "subghz.h"

#if defined(NEONDRIVE_TARGET_T_EMBED_CC1101)

#include "analyzer_sync.h"
#include <RadioLib.h>
#include <SD.h>
#include <SPI.h>

namespace SubGhz {
namespace {

// T-Embed CC1101 wiring (see src/neon_rf.cpp, main.cpp pin block)
constexpr int kCs   = 12;
constexpr int kGdo0 = 3;
constexpr int kGdo2 = 38;
constexpr int kSck  = 11;
constexpr int kMiso = 10;
constexpr int kMosi = 9;
constexpr int kSw1  = 47;   // antenna band select
constexpr int kSw0  = 48;

// Band limits of the CC1101
constexpr float kMinF = 300.0f;
constexpr float kMaxF = 928.0f;

// Packet-mode defaults (generic 433 MHz remote profile)
constexpr float kDefaultBr      = 4.8f;    // kbps
constexpr float kDefaultFreqDev = 5.0f;    // kHz
constexpr float kDefaultRxBw    = 135.0f;  // kHz
constexpr int8_t kDefaultPower = 10;      // dBm

constexpr uint8_t kMaxScanPoints = 96;
constexpr uint8_t kMaxPktLen     = 64;

SPIClass  subghzSpi(HSPI);
Module*   mod   = nullptr;
CC1101*   radio = nullptr;

bool     begun   = false;
bool     present = false;
volatile bool rxFlag = false;

Mode     curMode   = Mode::IDLE;
float    curFreq   = 0.0f;
uint32_t captures  = 0;
uint32_t txCount   = 0;
int16_t  lastRssi  = -128;
float    peakFreq  = 0.0f;
uint32_t jamUntilMs = 0;
uint32_t capSeq    = 0;
char     lastError[64] = {0};

uint8_t  lastPacket[kMaxPktLen] = {0};
uint8_t  lastPacketLen = 0;

struct ScanPoint { float f; int16_t rssi; };
ScanPoint scanTable[kMaxScanPoints];
uint8_t   scanCount = 0;

void setErr(const char* e) {
    strncpy(lastError, e, sizeof(lastError) - 1);
    lastError[sizeof(lastError) - 1] = '\0';
    Serial.printf("[subghz] error: %s\n", lastError);
}

void onRxFlag() { rxFlag = true; }

// Antenna switch: SW1=H,SW0=L → 315 MHz · H/H → 434 MHz · L/H → 868/915 MHz
void selectBand(float freqMhz) {
    bool sw1, sw0;
    if (freqMhz < 400.0f)      { sw1 = true;  sw0 = false; }
    else if (freqMhz < 700.0f) { sw1 = true;  sw0 = true;  }
    else                       { sw1 = false; sw0 = true;  }
    digitalWrite(kSw1, sw1 ? HIGH : LOW);
    digitalWrite(kSw0, sw0 ? HIGH : LOW);
}

float clampFreq(float f) {
    if (f < kMinF) return kMinF;
    if (f > kMaxF) return kMaxF;
    return f;
}

// Stage a SUBGHZ evidence session. Best-effort: SD may not be mounted.
bool stageSubghzSession(const char* kind) {
    AnalyzerSync::StageParams p{};
    p.module = "SUBGHZ";
    p.ssid   = kind;   // "scan" | "capture" — context label, not a WiFi target
    p.has_target = false;
    if (!AnalyzerSync::stageSession(SD, p)) {
        Serial.printf("[subghz] stage failed: %s\n",
                      AnalyzerSync::g_state.last_error);
        return false;
    }
    return true;
}

void writeArtifact(const char* name, const String& body) {
    if (AnalyzerSync::g_state.local_root[0] == '\0') return;
    char path[160];
    snprintf(path, sizeof(path), "%s/artifacts/%s",
             AnalyzerSync::g_state.local_root, name);
    File f = SD.open(path, FILE_WRITE);
    if (!f) return;
    f.print(body);
    f.close();
}

String wallclockLine() {
    char wall[28]; const char* src = "none";
    String s;
    if (AnalyzerSync::currentWallclockUtc(wall, sizeof(wall), src)) {
        s += "wallclock_utc="; s += wall; s += " ("; s += src; s += ")\n";
    }
    return s;
}

String hexOf(const uint8_t* data, uint8_t len) {
    String s;
    s.reserve(len * 2);
    for (uint8_t i = 0; i < len; i++) {
        if (data[i] < 0x10) s += '0';
        s += String(data[i], HEX);
    }
    return s;
}

bool ensureRadio() {
    if (!begun && !begin()) return false;
    return present;
}

} // namespace

bool available() { return begun && present; }

bool begin() {
    if (begun) return present;
    begun = true;

    pinMode(kSw1, OUTPUT);
    pinMode(kSw0, OUTPUT);
    selectBand(kBand434);

    // Begin our own handle on the shared HSPI bus. The SPI-parameterized
    // Module constructor will NOT re-begin it (initInterface=false).
    subghzSpi.begin(kSck, kMiso, kMosi, kCs);

    mod = new Module(kCs, kGdo0, RADIOLIB_NC, kGdo2,
                     subghzSpi, SPISettings(8000000, MSBFIRST, SPI_MODE0));
    radio = new CC1101(mod);

    int16_t st = radio->begin(kBand434, kDefaultBr, kDefaultFreqDev,
                              kDefaultRxBw, kDefaultPower, 16);
    if (st != RADIOLIB_ERR_NONE) {
        char e[64];
        snprintf(e, sizeof(e), "radio.begin failed (%d)", st);
        setErr(e);
        present = false;
        return false;
    }
    radio->standby();
    present = true;
    Serial.println("[subghz] CC1101 ready on shared HSPI bus");
    return true;
}

void end() {
    stop();
    if (radio) radio->sleep();
}

void stop() {
    if (radio && begun) {
        radio->standby();
        radio->setPacketReceivedAction(nullptr);
    }
    curMode = Mode::IDLE;
    rxFlag = false;
}

bool startScan(float f0Mhz, float f1Mhz, float stepMhz) {
    if (!ensureRadio()) return false;
    stop();
    f0Mhz = clampFreq(f0Mhz);
    f1Mhz = clampFreq(f1Mhz);
    if (f1Mhz < f0Mhz) { float t = f0Mhz; f0Mhz = f1Mhz; f1Mhz = t; }
    if (stepMhz < 0.05f) stepMhz = 0.05f;

    scanCount = 0;
    int16_t best = -128;
    peakFreq = f0Mhz;
    for (float f = f0Mhz; f <= f1Mhz && scanCount < kMaxScanPoints; f += stepMhz) {
        selectBand(f);
        if (radio->setFrequency(f) != RADIOLIB_ERR_NONE) continue;
        delay(6);  // let RSSI settle
        int16_t rssi = radio->getRSSI();
        scanTable[scanCount].f = f;
        scanTable[scanCount].rssi = rssi;
        scanCount++;
        if (rssi > best) { best = rssi; peakFreq = f; }
    }
    lastRssi = best;
    radio->standby();

    bool staged = stageSubghzSession("scan");
    String body = wallclockLine();
    body += "scan_mhz, rssi_dbm\n";
    for (uint8_t i = 0; i < scanCount; i++) {
        body += String(scanTable[i].f, 3);
        body += ",";
        body += scanTable[i].rssi;
        body += "\n";
    }
    if (staged) {
        writeArtifact("subghz_scan.csv", body);
        char msg[96];
        snprintf(msg, sizeof(msg),
                 "sub-GHz scan %.3f-%.3f MHz, %u points, peak %.3f MHz @ %d dBm",
                 f0Mhz, f1Mhz, scanCount, peakFreq, best);
        AnalyzerSync::appendEvent(SD, "subghz_scan", "SUBGHZ", "info", msg,
                                  "artifacts/subghz_scan.csv");
    }
    Serial.printf("[subghz] scan done: peak %.3f MHz @ %d dBm\n", peakFreq, best);
    return true;
}

bool startRx(float freqMhz) {
    if (!ensureRadio()) return false;
    stop();
    freqMhz = clampFreq(freqMhz);
    selectBand(freqMhz);
    int16_t st = radio->begin(freqMhz, kDefaultBr, kDefaultFreqDev,
                              kDefaultRxBw, kDefaultPower, 16);
    if (st != RADIOLIB_ERR_NONE) {
        char e[64]; snprintf(e, sizeof(e), "rx begin failed (%d)", st);
        setErr(e); return false;
    }
    radio->setPacketReceivedAction(onRxFlag);
    st = radio->startReceive();
    if (st != RADIOLIB_ERR_NONE) {
        char e[64]; snprintf(e, sizeof(e), "startReceive failed (%d)", st);
        setErr(e); return false;
    }
    curMode = Mode::RX;
    curFreq = freqMhz;
    captures = 0;
    capSeq = 0;
    stageSubghzSession("capture");
    Serial.printf("[subghz] RX on %.3f MHz\n", freqMhz);
    return true;
}

bool startReplay(float freqMhz, uint8_t times) {
    if (!ensureRadio()) return false;
    if (lastPacketLen == 0) { setErr("nothing captured to replay"); return false; }
    stop();
    freqMhz = clampFreq(freqMhz);
    selectBand(freqMhz);
    int16_t st = radio->begin(freqMhz, kDefaultBr, kDefaultFreqDev,
                              kDefaultRxBw, kDefaultPower, 16);
    if (st != RADIOLIB_ERR_NONE) { setErr("replay begin failed"); return false; }
    curMode = Mode::TX;
    curFreq = freqMhz;
    txCount = 0;
    if (times == 0) times = 1;
    for (uint8_t i = 0; i < times; i++) {
        if (radio->transmit(lastPacket, lastPacketLen) == RADIOLIB_ERR_NONE)
            txCount++;
        delay(50);
    }
    radio->standby();
    curMode = Mode::IDLE;
    char msg[96];
    snprintf(msg, sizeof(msg), "replayed %u-byte capture %lu/%u times @ %.3f MHz",
             lastPacketLen, (unsigned long)txCount, times, freqMhz);
    Serial.printf("[subghz] %s\n", msg);
    if (AnalyzerSync::g_state.session_id[0])
        AnalyzerSync::appendEvent(SD, "subghz_replay", "SUBGHZ", "info", msg);
    return txCount > 0;
}

bool startJam(float freqMhz, uint32_t durationMs) {
    if (!ensureRadio()) return false;
    stop();
    freqMhz = clampFreq(freqMhz);
    selectBand(freqMhz);
    int16_t st = radio->begin(freqMhz, kDefaultBr, kDefaultFreqDev,
                              kDefaultRxBw, kDefaultPower, 16);
    if (st != RADIOLIB_ERR_NONE) { setErr("jam begin failed"); return false; }
    if (durationMs < 1000) durationMs = 1000;
    if (durationMs > 60000) durationMs = 60000;  // lab-scoped cap
    curMode = Mode::JAM;
    curFreq = freqMhz;
    txCount = 0;
    jamUntilMs = millis() + durationMs;
    char msg[96];
    snprintf(msg, sizeof(msg), "sub-GHz jammer started @ %.3f MHz for %lus",
             freqMhz, (unsigned long)(durationMs / 1000UL));
    Serial.printf("[subghz] %s\n", msg);
    if (AnalyzerSync::g_state.session_id[0])
        AnalyzerSync::appendEvent(SD, "subghz_jam_start", "SUBGHZ", "warning", msg);
    return true;
}

void tick() {
    if (!radio || !begun) return;
    if (rxFlag && curMode == Mode::RX) {
        rxFlag = false;
        size_t len = radio->getPacketLength();
        if (len > kMaxPktLen) len = kMaxPktLen;
        int16_t st = radio->readData(lastPacket, len);
        if (st == RADIOLIB_ERR_NONE || st == RADIOLIB_ERR_CRC_MISMATCH) {
            lastPacketLen = (uint8_t)len;
            lastRssi = radio->getRSSI();
            captures++;
            capSeq++;
            char name[48];
            snprintf(name, sizeof(name), "subghz_cap_%03lu.txt",
                     (unsigned long)capSeq);
            String body = wallclockLine();
            body += "freq_mhz="; body += String(curFreq, 3); body += "\n";
            body += "rssi_dbm="; body += lastRssi; body += "\n";
            body += "len="; body += lastPacketLen; body += "\n";
            body += "crc="; body += (st == RADIOLIB_ERR_NONE ? "ok" : "mismatch"); body += "\n";
            body += "hex="; body += hexOf(lastPacket, lastPacketLen); body += "\n";
            bool staged = AnalyzerSync::g_state.session_id[0] != '\0';
            if (staged) {
                writeArtifact(name, body);
                char msg[96];
                snprintf(msg, sizeof(msg),
                         "sub-GHz packet #%lu: %u bytes @ %.3f MHz, %d dBm",
                         (unsigned long)captures, lastPacketLen, curFreq, lastRssi);
                char art[64];
                snprintf(art, sizeof(art), "artifacts/%s", name);
                AnalyzerSync::appendEvent(SD, "subghz_capture", "SUBGHZ",
                                          "info", msg, art);
            }
            Serial.printf("[subghz] cap #%lu: %u bytes rssi %d\n",
                          (unsigned long)captures, lastPacketLen, lastRssi);
        }
        radio->startReceive();  // re-arm
    }
    if (curMode == Mode::JAM) {
        if ((int32_t)(millis() - jamUntilMs) >= 0) {
            char msg[96];
            snprintf(msg, sizeof(msg),
                     "sub-GHz jammer stopped (%lu bursts)", (unsigned long)txCount);
            if (AnalyzerSync::g_state.session_id[0])
                AnalyzerSync::appendEvent(SD, "subghz_jam_stop", "SUBGHZ",
                                          "info", msg);
            stop();
        } else {
            // Noise-burst jamming: short random packets back-to-back.
            uint8_t noise[32];
            for (uint8_t i = 0; i < sizeof(noise); i++)
                noise[i] = (uint8_t)esp_random();
            if (radio->transmit(noise, sizeof(noise)) == RADIOLIB_ERR_NONE)
                txCount++;
        }
    }
}

Status status() {
    Status s{};
    s.present = begun && present;
    s.mode = curMode;
    s.freqMhz = curFreq;
    s.captures = captures;
    s.txCount = txCount;
    s.lastRssi = lastRssi;
    s.peakFreqMhz = peakFreq;
    strncpy(s.lastError, lastError, sizeof(s.lastError) - 1);
    return s;
}

String scanJson() {
    String out = "[";
    for (uint8_t i = 0; i < scanCount; i++) {
        if (i) out += ",";
        out += "{\"f\":"; out += String(scanTable[i].f, 3);
        out += ",\"rssi\":"; out += scanTable[i].rssi; out += "}";
    }
    out += "]";
    return out;
}

String lastCaptureHex() { return hexOf(lastPacket, lastPacketLen); }

} // namespace SubGhz

#endif // NEONDRIVE_TARGET_T_EMBED_CC1101
