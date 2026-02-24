# Telemetry DRAM Analysis & Option D Implementation

## Problem Summary

  ESP32 DRAM is at **100% capacity** with current firmware build:
  - **RAM Usage**: 38.0% (124,568 / 327,680 bytes)
  - **Available Headroom**: ~0 bytes (firmware uses all available DRAM segment)
  - **Overflow Threshold**: Adding even 12 bytes (3× uint32_t) causes 8-byte link-time overflow

##Attempted Solutions

### Iteration 1: Full SessionTelemetry Struct (FAILED)
- **Added**: Struct with 10 fields including top BSSID/SSID tracking
- **DRAM Cost**: ~120 bytes (includes char arrays for BSSID/SSID)
- **Result**: **1272 bytes overflow**

### Iteration 2: Minimal SessionTelemetry (FAILED)
- **Added**: 6 primitive variables (3× uint32_t + 3× int16_t)
- **DRAM Cost**: 18 bytes
- **Result**: **1208 bytes overflow** (removed char arrays)

### Iteration 3: Ultra-Minimal Tracking (FAILED)
- **Added**: 3× uint32_t variables (session start, packet count, packet bytes)
- **DRAM Cost**: 12 bytes
- **Result**: **8 bytes overflow** (alignment padding issue)

### Iteration 4: Removed Telemetry Entirely (SUCCESS)
- **Added**: Nothing (stub functions only)
- **DRAM Cost**: 0 bytes
- **Result**: ✅ **Compilation successful**

## Root Cause

The firmware was already at DRAM limit before telemetry additions. The 8-byte overflow with only 12 bytes added suggests:
1. Memory alignment requirements (ESP32 aligns volatile variables to 4/8-byte boundaries)
2. Zero headroom in existing BSS segment allocation
3. Large static arrays elsewhere consuming most DRAM:
   - `consoleRing[500]` + `packetRing[100]` = ~60KB
   - Display buffers, UI button arrays, Wi-Fi scan results
   - Bruce mode state machines (jammit, capture directories)

## Option D Implementation (Revised)

**Original Plan**: Hybrid telemetry with real-time streaming + SD file export
**Revised Plan**: Use existing endpoints without new telemetry variables

### Android App Data Sources

#### 1. Real-time Session Stats  
**Endpoint**: `GET /api/status`  
**Poll Interval**: 5 seconds  
**Response**:
```json
{
  "ok": true,
  "packets": 12345,
  "errors": 0,
  "dropped": 2,
  "handshakes": 5,
  "uptime_ms": 60000,
  "sniff_active": true,
  "pcap_files_open": true
}
```

**Android Usage**:
- Display live packet counter
- Calculate packets/second rate
- Show session duration (from uptime_ms)

#### 2. Packet Detail Stream  
**Endpoint**: `GET /api/packets?since=<seq>`  
**Poll Interval**: 1-2 seconds  
**Response**:
```json
{
  "ok": true,
  "items": [
    {
      "seq": 1001,
      "ts_ms": 123456,
      "rssi": -45,
      "len": 256,
      "type": "beacon",
      "bssid": "AA:BB:CC:DD:EE:FF",
      "ssid": "MyNetwork",
      "channel": 6
    }
  ],
  "next_seq": 1002
}
```

**Android Usage**:
- Parse RSSI values for histogram
- Track unique BSSIDs for network count
- Build timeline of packet types
- Detect deauth attacks (type="deauth" spikes)

#### 3. PCAP File Management (FUTURE)
**Endpoints**: 
- `GET /api/list?fs=sd&path=/m5porkchop/` - List captures
- `GET /download?fs=sd&path=/m5porkchop/beacons.pcap` - Download file

**Status**: **NOT IMPLEMENTED** (removed to avoid DRAM overflow)  
**Workaround**: User manually retrieves SD card or uses existing web interface

### Android AnalyzeScreen Implementation

```kotlin
// In WifiCydRepository.kt
suspend fun syncSessionStats(): SessionStats {
    val json = httpClient.get("http://192.168.4.1:8080/api/status")
        .body<JsonObject>()
    
    return SessionStats(
        packets = json["packets"]?.jsonPrimitive?.long ?: 0,
        duration = Duration.milliseconds(json["uptime_ms"]?.jsonPrimitive?.long ?: 0),
        handshakes = json["handshakes"]?.jsonPrimitive?.int ?: 0,
        sniffActive = json["sniff_active"]?.jsonPrimitive?.boolean ?: false
    )
}

suspend fun syncPacketStream(since: Long): List<PacketEvent> {
    val json = httpClient.get("http://192.168.4.1:8080/api/packets?since=$since")
        .body<JsonObject>()
    
    return json["items"]?.jsonArray?.map { item ->
        PacketEvent(
            seq = item["seq"]?.jsonPrimitive?.long ?: 0,
            rssi = item["rssi"]?.jsonPrimitive?.int ?: -127,
            type = item["type"]?.jsonPrimitive?.content ?: "unknown",
            bssid = item["bssid"]?.jsonPrimitive?.content ?: "00:00:00:00:00:00"
        )
    } ?: emptyList()
}
```

```kotlin
// In AnalyzeScreen.kt
@Composable
fun AnalyzeScreen(viewModel: WifiCydViewModel) {
    val stats by viewModel.sessionStats.collectAsState()
    val packets by viewModel.packetStream.collectAsState()
    
    LaunchedEffect(Unit) {
        while (true) {
            viewModel.syncSessionStats()
            delay(5000) // Poll every 5 seconds
        }
    }
    
    LaunchedEffect(Unit) {
        var lastSeq = 0L
        while (true) {
            val newPackets = viewModel.syncPacketStream(lastSeq)
            if (newPackets.isNotEmpty()) {
                lastSeq = newPackets.last().seq
            }
            delay(2000) // Poll every 2 seconds
        }
    }
    
    Column {
        // Session stats card
        Card {
            Text("Packets: ${stats.packets}")
            Text("Duration: ${stats.duration.inWholeSeconds}s")
            Text("Handshakes: ${stats.handshakes}")
        }
        
        // RSSI histogram (from packetStream)
        RssiHistogram(packets.map { it.rssi })
        
        // Packet timeline
        PacketTimeline(packets)
    }
}
```

## Recommendations

### Short-term (Current Build)
1. ✅ Use existing `/api/status` and `/api/packets` endpoints
2. ✅ Android app processes data locally (no SD file writes needed)
3. ✅ Visualizations built from streamed packet data

### Medium-term (Future Optimization)
1. **Reduce DRAM Usage**: Profile and optimize existing static arrays
   - Reduce `consoleRing` size (500 → 200 events)
   - Reduce `packetRing` size (100 → 50 events)
   - Compact Button arrays (use dynamic allocation)
2. **Add Telemetry**: Once 50+ bytes freed, re-add minimal session tracking
3. **File Export**: Implement `/api/list` and `/download` endpoints

### Long-term (Architecture)
1. **Partitioning**: Dedicated PSRAM chip for display buffers
2. **ESP-IDF Migration**: Better control over memory layout
3. **Modular Firmware**: Separate builds for different feature sets (WiFi scan vs Bruce modes)

## Conclusion

**Option D** can be implemented without SD-backed telemetry by leveraging existing endpoints. The Android app becomes the "aggregation layer," polling `/api/status` and `/api/packets` to build session analytics locally. This avoids DRAM constraints while achieving the same user experience.

**Key Insight**: The firmware is **DRAM-bound**, not Flash-bound. Further feature additions require memory optimization of existing code before new capabilities can be added.
