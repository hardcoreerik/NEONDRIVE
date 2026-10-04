#pragma once
// G3 — Live capture streaming (B3): Wireshark-compatible pcap stream over
// TCP while normal SD staging continues untouched.
//
// Design:
//   - StreamPcap::ingest() is called from the WiFi promiscuous RX callback.
//     It is RX-task safe: bounded memcpy into a static ring, no heap, no
//     blocking, no SD, no Serial. Copies only happen while at least one
//     stream client is connected (wantsPackets()).
//   - StreamPcap::tick() runs in loop(): accepts TCP clients on port 8081,
//     sends the pcap global header, then drains the ring as pcap records.
//   - Wireshark: `nc <device-ip> 8081 | wireshark -k -i -`
//     (linktype 105 = bare 802.11 frames, no radiotap).
//   - Ring-full drops are counted, never block the RX path.
//   - Packets are truncated to 256 bytes (mgmt/beacon-friendly).
#include <Arduino.h>
#include <stdint.h>

namespace StreamPcap {

constexpr uint16_t kStreamPort   = 8081;
constexpr uint8_t  kRingSlots    = 4;   // ~1 KB .bss; drops counted, never blocks
constexpr uint16_t kMaxCopyLen   = 256;   // per-packet truncation
constexpr uint8_t  kMaxClients   = 2;

// RX-task safe. No-op unless a client is connected.
void ingest(const uint8_t* payload, uint32_t len);

// Call once per loop().
void tick();

bool     anyClient();    // a stream client is connected
uint32_t dropped();      // ring-full drops since boot
uint8_t  clientCount();
String   statusJson();

} // namespace StreamPcap
