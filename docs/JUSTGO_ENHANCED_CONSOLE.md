# Just Go Enhanced Console Output

## Summary

The Just Go Live Console has been significantly enhanced to provide detailed, narrative feedback as automation progresses through targets and modes. Every state transition and major event now displays contextual information so users understand exactly what the device is doing.

---

## Console Output Examples

### Startup
```
=== JUST GO ===
Starting automation...
Spray:OFF|Stay:3min
```

### Scanning Phase
```
>>> SCANNING...
Found 5 targets
```

### Target Selection
```
>>> TARGET: CoffeeShop_5G
Auth:WPA2|Ch:6|RSSI:-45
```

### Y0INK Mode Engagement
```
[Y0INK]  Hunting handshakes...
```

**Live telemetry (updates every 300ms):**
```
Y0INK: 3C|0HS|0PMKID|15D|88s
Y0INK: 3C|1HS|0PMKID|45D|87s
Y0INK: 4C|2HS|1PMKID|120D|75s
```

**On Success:**
```
[SUCCESS] 2HS + 1PMKID
Y0INK complete - advancing...
```

**Or on Timeout:**
```
Y0INK timeout - found 4C/2HS
```

### JAMMIT Mode Engagement
```
[JAMMIT]  Launching deauth attack...
```

**Live telemetry (updates every 300ms):**
```
JAMMIT: 0C|5D|score:451|59s
JAMMIT: 0C|12D|score:576|54s
JAMMIT: 0C|22D|score:587|44s
```

**On Completion:**
```
JAMMIT complete - 22 deauths
```

### RAW Mode Engagement
```
[RAW]  Passive packet capture...
```

**Live telemetry (updates every 300ms):**
```
RAW: sniffer active | 44s
RAW: sniffer active | 30s
RAW: sniffer active | 15s
```

**On Completion:**
```
RAW complete - sniff done
```

### Target Transition
```
[TARGET DONE] 180s elapsed
Preparing next target...
```

### Stop/Abort
```
~~ STOPPED ~~
Ran for 234s
```

---

## Telemetry Breakdown

### Y0INK Format: `Xc|YHS|ZPMKID|WD|Vs`
- `Xc` = number of clients discovered
- `YHS` = handshakes captured
- `ZPMKID` = PMKIDs obtained
- `WD` = deauth frames sent
- `Vs` = seconds remaining in mode

### JAMMIT Format: `XC|YD|score:Z|Vs`
- `XC` = number of clients tracked
- `YD` = total deauth frames sent
- `score:Z` = packets per second (WiFi disruption score)
- `Vs` = seconds remaining in mode

### RAW Format: `sniffer active | Vs`
- `sniffer active` = status indicator
- `Vs` = seconds remaining in mode

---

## Serial Output Examples

All console events also log to USB serial with `[JUSTGO]` prefix:

```
[JUSTGO] === JUST GO ===
[JUSTGO] Starting automation...
[JUSTGO] Spray:OFF|Stay:3min
[JUSTGO-SCAN] targets_found=5
[JUSTGO-TARGET] ssid=CoffeeShop_5G bssid=F8:0D:A9:50:9F:85 auth=WPA2 ch=6 rssi=-45
[JUSTGO-ENGAGE] mode=Y0INK desc=Hunting handshakes...
[JUSTGO-Y0INK] clients=3 hs=0 pmkid=0 deauths=15 mode_remain=88s target_remain=180s
[JUSTGO-CAPTURE] handshakes=2 pmkids=1
[JUSTGO-ENGAGE] mode=JAMMIT desc=Launching deauth attack...
[JUSTGO-JAMMIT] clients=0 deauths=22 score=587 mode_remain=44s target_remain=154s
[JUSTGO-ENGAGE] mode=RAW desc=Passive packet capture...
[JUSTGO-RAW] sniffer_active=1 mode_remain=44s target_remain=110s
[JUSTGO] [TARGET DONE] 180s elapsed
[JUSTGO] Preparing next target...
[JUSTGO] ~~ STOPPED ~~
[JUSTGO] Ran for 234s
```

---

## Display Layout

**Live Console (6 visible lines, scrolling):**
```
╔════════════════════════════════════════╗
║ LIVE CONSOLE                           ║
├────────────────────────────────────────┤
│ >>> TARGET: CoffeeShop_5G              │
│ Auth:WPA2|Ch:6|RSSI:-45                │
│ [Y0INK]  Hunting handshakes...         │
│ Y0INK: 3C|1HS|0PMKID|45D|87s           │
│ Y0INK: 4C|2HS|1PMKID|120D|75s          │
│ [SUCCESS] 2HS + 1PMKID                 │
└────────────────────────────────────────┘
```

As new messages arrive, older ones scroll up and disappear, keeping the most recent 6 lines visible.

---

## Key Improvements

✅ **Clear State Narrative**
- Not just "Mode: Y0INK" but "Y0INK: Hunting handshakes..."
- User understands intent of each phase

✅ **Target Context**
- Shows SSID, auth type, channel, RSSI
- User knows what they're attacking

✅ **Event Severity Markers**
- `>>>` for target selection
- `[SUCCESS]` for handshake capture
- `~~ ~~` for stop events
- Visual cues help skim console

✅ **Progress Indicators**
- "found X targets" tells user scan success
- "Y0INK timeout - found AcHS" shows partial results
- Time elapsed on target completion

✅ **Dual Logging**
- Console for immediate visual feedback on device
- Serial for debugging via USB connection
- Both tell same story with consistent prefixes

---

## Testing Checklist

When you activate Just Go on your CYD device:

- [ ] Startup message shows current spray/stay settings
- [ ] ">>> SCANNING..." displays, found count appears
- [ ] ">>> TARGET: SSID" shows on selection with auth/channel/RSSI
- [ ] Mode engagement shows "[MODE]  Description"
- [ ] Telemetry updates appear every ~300ms without flicker
- [ ] Mode transitions show summary (e.g., "Y0INK timeout - found 3C/1HS")
- [ ] Target hop shows "[TARGET DONE] elapsed seconds"
- [ ] Serial monitor shows corresponding [JUSTGO-*] prefixed logs
- [ ] Pressing STOP shows "~~ STOPPED ~~" and elapsed time

---

## No DRAM Impact

- **Before:** 37.9% RAM (124,328 / 327,680 bytes)
- **After:** 37.9% RAM (124,312 / 327,680 bytes)
- **Delta:** -16 bytes (enhanced strings are printed, not stored longer)
- **Memory:** Stack-allocated buffers (~96 bytes) only live during snprintf calls

---

## Future Enhancements

- Client signal quality (RSSI per client)
- Estimated time to goal (X targets × Y stay time remaining)
- Audio feedback on successful captures
- Progress bar for mode completion
- Success summary with capture count
