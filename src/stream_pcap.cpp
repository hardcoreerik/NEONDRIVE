// G3 — Live pcap streaming over TCP (B3). Clean-room implementation.
#include "stream_pcap.h"

#include <WiFi.h>

namespace StreamPcap {
namespace {

// 24-byte pcap global header, linktype 105 (IEEE 802.11, bare frames).
struct GlobalHeader {
    uint32_t magic   = 0xa1b2c3d4;
    uint16_t verMaj  = 2;
    uint16_t verMin  = 4;
    int32_t  tz      = 0;
    uint32_t sigfigs = 0;
    uint32_t snaplen = kMaxCopyLen;
    uint32_t network = 105;
};

// 16-byte per-packet record header.
struct RecHeader {
    uint32_t tsSec;
    uint32_t tsUsec;
    uint32_t inclLen;
    uint32_t origLen;
};

struct Slot {
    RecHeader hdr;
    uint8_t   data[kMaxCopyLen];
};

Slot          ring[kRingSlots];
volatile uint8_t head = 0;   // written by RX task
volatile uint8_t tail = 0;   // written by loop task
portMUX_TYPE  ringMux = portMUX_INITIALIZER_UNLOCKED;

WiFiServer  server(kStreamPort);
WiFiClient  clients[kMaxClients];
bool        serverBegun = false;
uint32_t    dropCount = 0;
volatile bool wantCopy = false;

uint8_t nextIdx(uint8_t i) { return (uint8_t)((i + 1) % kRingSlots); }

bool sendAll(WiFiClient& c, const uint8_t* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        if (!c.connected()) return false;
        size_t n = c.write(buf + sent, len - sent);
        if (n == 0) return false;  // would-block / error → drop client
        sent += n;
    }
    return true;
}

void pruneClients() {
    for (uint8_t i = 0; i < kMaxClients; i++) {
        if (clients[i] && !clients[i].connected()) clients[i].stop();
    }
}

uint8_t liveClients() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < kMaxClients; i++)
        if (clients[i] && clients[i].connected()) n++;
    return n;
}

} // namespace

void ingest(const uint8_t* payload, uint32_t len) {
    if (!wantCopy || !payload || len == 0) return;
    uint32_t copyLen = len > kMaxCopyLen ? kMaxCopyLen : len;
    uint32_t ms = millis();

    portENTER_CRITICAL(&ringMux);
    uint8_t h = head;
    uint8_t nx = nextIdx(h);
    if (nx == tail) {
        dropCount++;              // ring full: count, never block RX
        portEXIT_CRITICAL(&ringMux);
        return;
    }
    Slot& s = ring[h];
    s.hdr.tsSec   = ms / 1000UL;
    s.hdr.tsUsec  = (ms % 1000UL) * 1000UL;
    s.hdr.inclLen = copyLen;
    s.hdr.origLen = len;
    memcpy(s.data, payload, copyLen);
    head = nx;
    portEXIT_CRITICAL(&ringMux);
}

void tick() {
    if (!serverBegun) {
        server.begin();
        server.setNoDelay(true);
        serverBegun = true;
    }

    // Accept new clients.
    WiFiClient nc = server.available();
    if (nc) {
        bool placed = false;
        for (uint8_t i = 0; i < kMaxClients && !placed; i++) {
            if (!clients[i] || !clients[i].connected()) {
                if (clients[i]) clients[i].stop();
                clients[i] = nc;
                placed = true;
                GlobalHeader gh;
                if (!sendAll(clients[i],
                             reinterpret_cast<const uint8_t*>(&gh),
                             sizeof(gh))) {
                    clients[i].stop();
                } else {
                    Serial.println("[stream] pcap client connected");
                }
            }
        }
        if (!placed) nc.stop();  // at capacity
    }

    pruneClients();
    wantCopy = liveClients() > 0;
    if (!wantCopy) return;

    // Drain the ring to every live client.
    for (;;) {
        uint8_t t;
        Slot s;
        portENTER_CRITICAL(&ringMux);
        if (tail == head) { portEXIT_CRITICAL(&ringMux); break; }
        t = tail;
        s = ring[t];              // struct copy under lock (~272 B)
        tail = nextIdx(t);
        portEXIT_CRITICAL(&ringMux);

        for (uint8_t i = 0; i < kMaxClients; i++) {
            if (clients[i] && clients[i].connected()) {
                bool ok = sendAll(clients[i],
                                  reinterpret_cast<const uint8_t*>(&s.hdr),
                                  sizeof(s.hdr)) &&
                          sendAll(clients[i], s.data, s.hdr.inclLen);
                if (!ok) clients[i].stop();
            }
        }
        yield();
    }
    wantCopy = liveClients() > 0;
}

bool anyClient() { return wantCopy; }
uint32_t dropped() { return dropCount; }
uint8_t clientCount() { return liveClients(); }

String statusJson() {
    String out = "{\"ok\":true,\"port\":";
    out += kStreamPort;
    out += ",\"clients\":"; out += liveClients();
    out += ",\"dropped\":"; out += dropCount;
    out += "}";
    return out;
}

} // namespace StreamPcap
