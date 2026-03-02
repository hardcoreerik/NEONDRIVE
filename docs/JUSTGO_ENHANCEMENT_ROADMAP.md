# Just Go Enhancement Roadmap - Additional Attack Functions

## Current Status

**Currently Implemented in Just Go:**
1. **Y0INK** - WPA/WPA2 Handshake & PMKID Capture (90s)
2. **JAMMIT** - Targeted Deauth Attack with WSL bypasser (45s)
3. **RAW** - Passive Packet Sniffer (45s)

**Serial Output Shows:**
```
[JUSTGO-Y0INK] clients=4 hs=2 pmkid=1 deauths=640 mode_remain=62s target_remain=152s
[JUSTGO-JAMMIT] clients=0 deauths=22 score=587 mode_remain=44s target_remain=110s
[JUSTGO-RAW] sniffer_active=1 mode_remain=44s target_remain=100s
```

---

## Available But Unused Attack Functions

### 1. **BEACON SPAM** (BACON Mode)
**What It Does:**
- Broadcasts fake beacon frames on specific channel
- Spoofs AP fingerprints with vendor IE (OUI: 0x50:0x52:0x4B "PRK")
- Can impersonate existing APs or create fake networks
- Broadcasts top 3 APs with RSSI fingerprinting

**Why Add to Just Go:**
- Network enumeration (discover hidden SSIDs)
- Detect clients seeking specific networks
- Network name spoofing for client testing
- Non-intrusive (no deauth, just beacons)

**Proposed Integration:**
```
Attack Sequence Option A (Current):
Y0INK (90s) → JAMMIT (45s) → RAW (45s)

Attack Sequence Option B (Enhanced - User Toggle):
BEACON (30s) → Y0INK (90s) → JAMMIT (45s) → RAW (45s)
                (find hidden networks, attract clients)
```

**Implementation:**
- Add `BEACON_SPAM` to `enum AutoMode`
- Duration: 30-45 seconds per target
- Better for open/WPA2 networks or finding hidden SSIDs
- Pairs well with JAMMIT (creates signal for clients to find)

---

### 2. **PROBE FLOOD** (BRUCE Mode)
**What It Does:**
- Sends WiFi probe requests (client-like behavior)
- Floods a channel to confuse AP detection
- Simulates many clients searching for networks
- No authentication involved

**Why Add to Just Go:**
- Acts as client simulator before handshake hunt
- Can trigger AP responses revealing capabilities
- Safer than deauth (no disruption)
- Low computational load

**Proposed Integration:**
```
Time Sequence:
PROBE_FLOOD (30s) → Y0INK (90s) → JAMMIT (45s) → RAW (45s)
└─ "Scanning for clients"  "Hunting handshakes"
```

**Implementation:**
- Add `PROBE_FLOOD` to `enum AutoMode`
- Duration: 30 seconds before handshake hunt
- Helps Y0INK find clients faster
- Serial output: `[JUSTGO-PROBE] pps=X probes_sent=Y`

---

### 3. **DEAUTH_FLOOD** (BRUCE Mode - Full Channel Flood)
**What It Does:**
- Sends deauth frames to broadcast address
- Floods ALL devices on channel (not just target)
- More aggressive than JAMMIT (which targets specific clients)
- High power mode

**Why Add to Just Go:**
- Alternative for open networks (no clients to target)
- More chaotic/complete disruption
- Some devices respond better to broadcast deauth
- Pre-scan for available targets

**Proposed Integration:**
```
For OPEN networks:
DEAUTH_FLOOD (45s) → RAW (45s)  [Skip Y0INK/JAMMIT]
```

**Implementation:**
- Add `DEAUTH_FLOOD` to `enum AutoMode`
- Only for open/WEP auth types
- Serial: `[JUSTGO-DEAUTH_FLOOD] pps=X deauths=Y target=broadcast`

---

### 4. **PASSIVE RECON** (DONOHAM Mode - Enhanced Scanning)
**What It Does:**
- Adaptive channel hopping based on activity
- Tracks incomplete handshakes across sessions
- Stores PMKID/handshake references
- Multi-phase state machine: HOPPING → DWELLING → HUNTING

**Why Add to Just Go:**
- **Pre-scan phase** before attacking
- Better target selection (know what's on each channel)
- Identify high-activity targets first
- Track handshake likelihood

**Proposed Integration:**
```
Initial Scan → Target Selection → Attack Sequence

Add:
RECON (60s) → identify best targets
↓
Then proceed to attack sequence
```

**Implementation:**
- Adaptive heuristics: detect busy channels
- Log channel activity statistics
- Console: `[JUSTGO-RECON] ch6=busy(8beacons) ch11=dead ch1=quiet`

---

### 5. **PMKID-Focused Hunting** (Optimize Y0INK)
**Currently Y0INK searches for:**
- Handshakes (4-way/3-way)
- PMKIDs

**Enhancement:**
- Fast PMKID hunt before full handshake
- Station forced disconnect → PMKID response
- Shorter timeout when PMKID found (abandon handshake hunt after 20s if PMKID acquired)

**Implementation:**
- New mode: `PMKID_ASSAULT` (30s aggressive PMKID hunt)
- Send SA Query frames to trigger PMKID reply
- Serial: `[JUSTGO-PMKID] recovered=1 time_to_capture=8s`

---

## Proposed Just Go "Super Sequence" 

### **Mode: FULL_PENETRATION** (New)
```
Total Time Per Target: ~5-7 minutes (user configurable)

Sequence:
1. BEACON_PROBE (30s)
   → Broadcast beacons to attract clients
   Console: "[BEACON] Broadcasting on ch6, 45 frames/sec"
   
2. PROBE_FLOOD (30s)
   → Simulate client searches
   Console: "[PROBE] Flooding 120 probes/sec, clients alert?"
   
3. Y0INK (90s)
   → Capture handshakes/PMKIDs while clients active
   Console: "[Y0INK] 5C|0HS|2PMKID|340D|67s"
   
4. JAMMIT (45s)
   → Targeted deauth to confirmed clients
   Console: "[JAMMIT] 3 device deauth bursts, score: 587"
   
5. PMKID_RECON (15s)
   → Focused PMKID force
   Console: "[PMKID] SA Query sent, await response"
   
6. RAW (45s)
   → Passive capture of remnant traffic
   Console: "[RAW] 12,450 packets captured"
   
Total: ~4-5 min per target with rolling attack phases
```

---

## Implementation Priority

### **Tier 1 (Recommended First)**
1. ✅ Enhance existing Y0INK output (per serial show - DONE)
2. ➕ Add **PROBE_FLOOD** (30s pre-scan)
   - Minimal code (reuse existing functions)
   - Works well before Y0INK
   - Serial logging easy to add

### **Tier 2 (Medium Effort)**
3. ➕ Add **BEACON_SPAM** (alternative for hidden networks)
   - Use bacon.h as reference
   - 30-45s duration
   - Reusable beacon TX code

4. ➕ Add **PMKID_ASSAULT** (aggressive PMKID hunt)
   - Optimize Y0INK state machine
   - Add SA Query frame transmission
   - More aggressive timing

### **Tier 3 (Advanced)**
5. ➕ Add **PASSIVE_RECON** phase (pre-attack)
   - Channel activity analysis
   - Best-target recommendation
   - Adaptive targeting based on likelihood

6. ➕ Add **DEAUTH_FLOOD** (alternative for open auth)
   - Broadcast-only deauth variant
   - For networks with no discoverable clients

---

## New Enhancements to Just Go Config

### **Attack Strategy Selection**
Currently:
```
Spray+Pray: ON/OFF
Stay: 3-15 minutes
```

Proposed:
```
Spray+Pray: ON/OFF
Stay: 3-15 minutes
↓ NEW ↓
Attack Profile: 
  ☐ STANDARD (Y0INK→JAMMIT→RAW)
  ☐ BEACON_FIRST (BEACON→Y0INK→JAMMIT→RAW)
  ☐ AGGRESSIVE (PROBE→Y0INK→PMKID→JAMMIT→RAW)
  ☐ STEALTH (Y0INK only, no deauth)
  ☐ CHAOS (DEAUTH_FLOOD→RAW)

Target Filter:
  ☐ All Networks
  ☐ WPA/WPA2 Only
  ☐ Open Networks Only
  ☐ Hidden SSIDs Only
```

---

## Console Output Enhancements

### **For PROBE_FLOOD**
```
[JUSTGO-PROBE] Starting probe flood on ch6
[JUSTGO-PROBE] 120 probes/sec | 3,600 total
[JUSTGO-PROBE] Probe flood complete
```

### **For BEACON_SPAM**
```
[JUSTGO-BEACON] Spoofing top 3 APs with vendor IE
[JUSTGO-BEACON] Beacon rate: 45 frames/sec
[JUSTGO-BEACON] Beacons transmitted: 1,350
```

### **For PMKID_ASSAULT**
```
[JUSTGO-PMKID] Launching aggressive PMKID hunt
[JUSTGO-PMKID] SA Query burst sent to 3 targets
[JUSTGO-PMKID] PMKID recovered in 8.2s
[JUSTGO-PMKID] Advancing to JAMMIT (PMKID secured)
```

### **For Passive Recon**
```
[JUSTGO-RECON] Ch1=4b/2e | Ch6=8b/1e | Ch11=6b/3e
[JUSTGO-RECON] Best targets: ch6 (busy), ch1 (moderate)
[JUSTGO-RECON] Recon complete, 12 networks analyzed
```

---

## Memory Impact Analysis

| Component | DRAM Cost | Notes |
|-----------|-----------|-------|
| Current Just Go | ~200B | State vars, buffers |
| + PROBE_FLOOD | ~50B | Probe counter, timer |
| + BEACON_SPAM | ~400B | Beacon frame buffer, AP list |
| + PMKID state | ~100B | Aggressive hunt flag, timing |
| + Recon stats | ~200B | Channel activity tracking |
| **Total Estimated** | ~1KB | Within remaining 62KB headroom |

**Current:** 37.9% RAM usage with 62KB headroom  
**With all enhancements:** Still <<40% (well within limits)

---

## Next Steps

1. **Short Term:** Add PROBE_FLOOD + enhanced PMKID logic to Y0INK
2. **Medium Term:** Integrate BEACON_SPAM as alternative mode
3. **Long Term:** User-selectable attack profiles + passive recon

---

## Questions for Implementation

1. Should PROBE_FLOOD run BEFORE Y0INK or AFTER JAMMIT?
2. Do we want BEACON_SPAM to imitate target SSID or broadcast fake SSIDs?
3. Should PMKID_ASSAULT be its own 30s mode or part of Y0INK?
4. Desired attack profile defaults: STANDARD or AGGRESSIVE?
5. Should recon phase auto-skip targets with no activity detected?

