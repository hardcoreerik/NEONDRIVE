# Live Capture Streaming (G3)

Streams captured 802.11 frames over TCP as a **Wireshark-compatible pcap
stream**, while normal SD staging continues in parallel.

## Usage

1. Start a capture (Packet Lab or `/api/packetlab/...`).
2. On a host on the same network:

   ```
   nc <device-ip> 8081 | wireshark -k -i -
   ```

   The device status page `/stream` shows the exact command with the current
   IP. Wireshark parses the stream live (linktype 105 = bare 802.11).

## Design (RAM-first)

- **RX-safe ingest:** `StreamPcap::ingest()` is called from the promiscuous
  WiFi callback. It copies at most 256 bytes into a static 4-slot ring under
  a spinlock — no allocation, no SD I/O, no blocking. When no client is
  connected, ingest is a single branch (zero cost).
- **Drain in `loop()`:** `StreamPcap::tick()` writes queued frames to the
  TCP client, at most 4 frames per tick, ~2 ms budget per tick. Backpressure
  **drops from the ring** (counter exposed on `/api/stream/status`) — capture
  is never slowed by a slow consumer.
- Static RAM cost ≈ **1.2 KB** (4 × (256 + 8) bytes + header + socket state).

## Status

`GET /api/stream/status` → clients, frames sent, bytes sent, dropped count,
ring capacity, port.
