# Android Data Sync & Analysis Integration

## Current State

**Firmware Generates:**
- CSV files with Wi-Fi scan data (SSID, BSSID, RSSI, channel, security type, vendor OUI)
- PCAP captures (raw 802.11 frames from promiscuous mode)
- Handshake files (EAPOL key exchanges, WPA credentials)
- Session logs (text + JSON telemetry)
- Attack telemetry (Yoink engine: injection counts, channel dwell times, success metrics)

**Android App Current State:**
- AnalyzeScreen (DATA tab): Placeholder with session summaries
- File manager (ExportScreen): Basic SD browser, no analysis
- AI chat: Authorized-pentester domain model only
- No packet inspection, no tshark integration, no visualization

---

## Strategic Options

### Option A: **Lazy Sync on Demand**
*What it is:* User taps "SYNC" button → downloads new files → filters locally → displays in UI

**Workflow:**
```
User taps SYNC
  ↓
App calls /api/list on /data and /handshakes
  ↓
Filters by:
  - Modified date (newer than last sync)
  - File type (*.csv, *.pcap, *.json)
  - Size threshold (skip very large captures)
  ↓
Downloads to app cache/Documents
  ↓
Parses CSVs → populate analytics
  ↓
AI engine indexes data (LLM embedding for pentesting context)
  ↓
User can query "what networks look vulnerable?"
```

**Pros:**
- Low bandwidth footprint (lazy loading)
- Intuitive: one-tap refresh
- Works offline once downloaded
- Min runtime overhead on CYD
- Clean separation: firmware generates, app consumes

**Cons:**
- Network latency on first sync
- Must implement CSV/PCAP parsing on Android (no external tools)
- tshark not applicable (works on desktop, not mobile)
- Large PCAP files need streaming or chunking

---

### Option B: **Continuous Background Sync + Event Streaming**
*What it is:* App maintains background sync job; firmware publishes "new packet" events; Android updates dashboard in real-time

**Workflow:**
```
App starts → subscribes to WebSocket /api/events
  ↓
Firmware emits: { "event": "packet_captured", "timestamp": 1234, "rssi": -45, "ssid": "Evil" }
  ↓
App buffers events locally in SQLite
  ↓
UI updates live graph: RSSI vs Time, Channel frequency heatmap, Network list
  ↓
After session ends:
  - Download full session metadata
  - AI indexes for pentesting suggestions
  - Store searchable DB locally
```

**Pros:**
- Real-time dashboards (sniff progress, target RSSI trends)
- Reduces bandwidth: only new deltas
- Immersive experience (live attack telemetry)
- Can overlay attack metrics with frame data
- Supports live alerts (e.g., "WPA3 network detected")

**Cons:**
- WebSocket connection overhead on CYD (single radio, competing with Wi-Fi)
- Requires careful memory budgeting (SDCard logging vs RAM)
- More firmware complexity
- Tshark still inapplicable (no CLI tool for mobile)

---

### Option C: **Export to PCAPI Format for External Analysis**
*What it is:* Export PCAP locally on phone → integrate with Android packet sniffer library → perform tshark-like analysis on-device

**Workflow:**
```
User downloads PCAP from CYD → Stored in app's Documents/
  ↓
App loads PCAP natively:
  - Use PcapDroid library (Android packet capture library)
  - Or SharkPy (Python via Pydroid, if available)
  - Or built-in packet parser (manual frame dissection)
  ↓
Frame-by-frame analysis:
  - MAC/IP stats
  - Protocol distribution (HTTP, DNS, TLS handshakes)
  - Anomaly detection (unusual TTLs, fragmentation)
  - WPA/WEP key material extraction
  ↓
AI engine reads: "This PCAP shows 3 WPA2 networks with EAP-MSCHAPV2, vulnerable to ASLEAP"
  ↓
Visualization: Protocol pie chart, IP histogram, timeline of auth attempts
```

**Pros:**
- True tshark-like packet dissection on Android
- Offline analysis (no CYD connection needed after download)
- Leverages existing Android packet libraries
- Can export analysis as PDF report
- Integrates with AI engine for recommended attacks

**Cons:**
- PCAP parsing is heavy; performance depends on file size
- PcapDroid/SharkPy adds app size (~10–30 MB)
- Requires user to manually request download
- Network bandwidth for large PCAPs

---

### Option D: **Hybrid: Streamed Summary + Bulk Export**
*What it is:* Firmware sends aggregated stats in real-time (JSON summary), full PCAP on demand

**Workflow:**
```
Real-time (every 5 seconds):
  Firmware → /api/telemetry returns:
    { "packets_captured": 1024, "bytes": 102400, "unique_bssids": 12, 
      "rssi_avg": -65, "top_network": "Starbucks", "alerts": [ "WPS_enabled" ] }
  ↓
  App updates dashboard: gauge, alerts, histogram
  
On-demand (after session):
  User taps "Export PCAP for Analysis"
    ↓
    App downloads full PCAP (chunked for large files)
    ↓
    Locally parses with frame-by-frame decoder
    ↓
    Generates insights: "Found 847 deauths → AP is vulnerable to de-auth attacks"
    ↓
    AI suggests: "Try Yoink deauth+handshake capture next"
```

**Pros:**
- Best of both worlds: real-time + deep analysis
- Efficient network usage (summaries are ~1 KB, PCAP is on-demand)
- Scales well to long sessions (telemetry stream is bounded)
- Tshark-equivalent analysis without external tools
- CYD can log directly to SD; app just reads summaries

**Cons:**
- Firmware needs to emit telemetry JSON (small change to main.cpp)
- App parsing logic is moderate complexity
- Requires robust error handling (network dropouts, partial downloads)

---

### Option E: **Federated Desktop + SPY Mode (Most Ambitious)**
*What it is:* Phone acts as remote terminal for PC-hosted tshark analysis

**Workflow:**
```
App on phone → connects to CYD via Wi-Fi
  ↓
CYD runs: tshark reading from promiscuous tap
  ↓
CYD sends packet stream to phone via UDP/WebSocket
  ↓
Phone relays to laptop over Wi-Fi (if nearby) or internet (if VPN tunnel)
  ↓
Laptop runs `tshark -i <stream>` → full GUI analysis
  ↓
Results fed back to phone UI
```

**Pros:**
- True tshark power (enterprise-grade packet dissection)
- Laptop can do heavy lifting (GeoIP mapping, signature detection)
- Suitable for legitimate pentesten audits
- Scales to very large PCAP files

**Cons:**
- Requires laptop on same network (or VPN)
- Massive complexity (3-device sync)
- Overkill for casual analysis
- Not mobile-first experience

---

## Recommended Path (My Preference)

### **Option D + Visualization Layer** ✨

**Why:**
1. **Intuitive workflow**: One-tap sync → dashboard shows live progress → explore deep data
2. **AI-ready**: Android AI engine can read parsed packets and generate contextual pentesting hints
3. **Scalable**: Summaries keep UI responsive; heavy lifting happens on-demand
4. **Realistic**: Aligns with how actual pen-testers work (monitor in real-time, analyze after)
5. **Firmware-light**: Minimal changes to CYD (just add `/api/telemetry` JSON endpoint)

**Implementation Stack:**
- **Firmware**: Add `/api/telemetry` endpoint returning { packets, bytes, rssi_avg, top_networks, alerts }
- **Android Repository**: New method `syncAndAnalyze()` that downloads files + parses locally
- **Android Parser**: Custom frame dissector for 802.11 (lightweight, no external deps vs SharkPy)
- **Analytics Engine**: SQLite DB storing parsed packets; queries for "networks by vulnerability"
- **Visualization**: Material3 charts (Bar, Line, Pie) showing:
  - Timeline of packet counts
  - RSSI heatmap (channel × time)
  - Top 10 BSSIDs by activity
  - Security posture (WPA3 vs WPA2 vs Open)
  - AI-flagged alerts (deauth storms, weak ciphers)
- **AI Integration**: AI engine reads parsed packets + session metadata → suggests next attack vector

---

## Data Flow Diagram

```
┌─────────────────┐
│  CYD Firmware   │
├─────────────────┤
│ PCAP (raw 802.11) → /api/download (on-demand)
│ CSV (network summary) → /api/list
│ Telemetry JSON → /api/telemetry (streamed)
└────────┬────────┘
         │
         │ HTTP/1.1
         ↓
┌─────────────────────────────────────┐
│  Android App (SYNC workflow)        │
├─────────────────────────────────────┤
│ 1. Repository.syncAndAnalyze()      │
│    ├─ downloadTelemetry()           │ ← Real-time progress
│    ├─ downloadNewFiles()            │ ← CSV + PCAP buffers
│    └─ parseFrames()                 │ ← Frame dissection
│                                     │
│ 2. Analytics Engine                 │
│    ├─ SQLite: frame store           │
│    ├─ Query: "vulnerable SSIDs"     │
│    └─ Export: PDF report            │
│                                     │
│ 3. Visualization Layer              │
│    ├─ Packet timeline               │
│    ├─ RSSI heatmap                  │
│    ├─ Security posture pie          │
│    └─ AI-generated insights card    │
│                                     │
│ 4. AI Integration                   │
│    └─ Chat: "What can I attack?"    │ ← LLM context on parsed data
└─────────────────────────────────────┘
```

---

## UX Flow (Tab: "DATA")

**State 1: Idle (no sync yet)**
```
┌─────────────────────────┐
│  DATA                   │
├─────────────────────────┤
│ [💾 SYNC]               │
│                         │
│ No data downloaded yet  │
│ Tap SYNC to begin       │
└─────────────────────────┘
```

**State 2: Syncing**
```
┌─────────────────────────┐
│  DATA (Syncing...)      │
├─────────────────────────┤
│ ⏳ Downloading packets...│
│ 256 / 512 frames        │
│ ⬇️  2.3 MB/s            │
│                         │
│ [Cancel]                │
└─────────────────────────┘
```

**State 3: Complete**
```
┌──────────────────────────────┐
│  DATA                        │
├──────────────────────────────┤
│ ✅ Synced 847 packets       │
│ [🔄 REFRESH]                │
│                              │
│ ┌─ LIVE TELEMETRY ────────┐ │
│ │ Packets: 847     Duration│ │
│ │ Bytes: 234 KB    00:02:31│ │
│ │ Avg RSSI: -62 dBm       │ │
│ └─────────────────────────┘ │
│                              │
│ ┌─ TOP NETWORKS ──────────┐ │
│ │ Starbucks-5G (WPA2)  -45│ │
│ │ Home-WiFi (WPA3)    -68 │ │
│ │ OpenNet (Open)      -72 │ │
│ └─────────────────────────┘ │
│                              │
│ ┌─ INSIGHTS ──────────────┐ │
│ │ ⚠️  Deauth storm detected│ │
│ │ 🔓 1 open network found │ │
│ │ 📊 Security: 2/3 WPA3   │ │
│ └─────────────────────────┘ │
│                              │
│ [📈 CHARTS] [🤖 ASK AI]     │
└──────────────────────────────┘
```

**State 4: Expanded Charts**
```
Packet Count (Timeline)
  |
  |  ╱╲  ╱╲
  | ╱  ╲╱  ╲___
  |
  └─────────────► Time

RSSI Heatmap
  Ch 1 ████████ -45 dBm
  Ch 6 ██████   -52 dBm
  Ch 11 ████    -68 dBm

Security Posture
  ┌─────────┐
  │ WPA3 30%│   Open 10%
  │ WPA2 60%│   ╱
  └─────────┘
```

---

## Implementation Checklist

### Firmware (CYD)
- [ ] Add `/api/telemetry` endpoint (returns real-time stats JSON)
- [ ] Add `/api/download` support for PCAP files (with chunking for large files)
- [ ] Expose packet count, bytes, RSSI avg, top BSSID from live scan session

### Android Repository Layer
- [ ] `syncAndAnalyze(onProgress: (percent) -> Unit)` suspend function
- [ ] Frame dissector for 802.11 (lightweight, ~300 LOC)
- [ ] SQLite schema for parsed frames (timestamp, rssi, ssid, bssid, protocol)
- [ ] Query builders for "vulnerable networks", "deauth storms", etc.

### Android UI (AnalyzeScreen)
- [ ] Sync button + progress indicator
- [ ] Real-time telemetry card (packets, bytes, duration)
- [ ] Top networks card (table with RSSI icons)
- [ ] Charts: Packet timeline, RSSI heatmap, security pie
- [ ] Insights card: AI + rule-based alerts

### Android AI Integration
- [ ] Feed parsed packet data to `AiEngineClient.chat()`
- [ ] Custom system prompt for packet analysis ("You are a pentester analyzing a PCAP...")
- [ ] Cache chat results in SQLite for Q&A exploration

### Testing
- [ ] Mock PCAP generator for offline testing
- [ ] Verify performance with 10k+ frame PCAP
- [ ] Test chunked downloads on slow network
- [ ] Verify AI suggestions are contextual to packet data

---

## Next Steps

**Pick your favorite option(s) above, and I'll:**
1. Build the firmware telemetry endpoint
2. Create the Android frame parser + SQLite schema
3. Wire up UI cards with Material3 charts
4. Integrate AI analysis context
5. Test end-to-end on real hardware

What resonates most with your workflow? 🚀
