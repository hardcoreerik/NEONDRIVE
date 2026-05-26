# Wardrive Analyzer Sync

NEONDRIVE collects wireless validation evidence in the field. Wardrive Analyzer
imports synced session bundles on the PC for scoring, reporting, and remediation
guidance.

Dropbox is the bridge between them.

---

## Product Loop

```
NEONDRIVE device
  └─ Runs capture module (SP3CTER / Y0INK / JAMMIT / etc.)
  └─ Technician visits /analyzer web page
  └─ Clicks "Stage Evidence Bundle" → writes session folder to SD
  └─ Clicks "Sync to Dropbox" → uploads bundle files, then sync_complete.flag

Dropbox Desktop App
  └─ Syncs remote folder to PC automatically

Wardrive Analyzer PC
  └─ Technician clicks "Import NEONDRIVE Session…" in SD Ingest tab
  └─ Selects the local Dropbox-synced session folder
  └─ Wardrive Analyzer validates, deduplicates, and ingests evidence
  └─ Runs analysis: PCAP parsing, handshake confidence, reporting
```

---

## Web Routes

| Route | Method | Purpose |
|---|---|---|
| `/analyzer` | GET | Technician sync page |
| `/api/analyzer/status` | GET | JSON sync state |
| `/api/analyzer/stage` | POST | Build session bundle on SD |
| `/api/analyzer/sync` | POST | Upload bundle to Dropbox |
| `/api/analyzer/sessions` | GET | List sessions on SD |
| `/api/analyzer/manifest?session=<id>` | GET | Return manifest.json |

---

## Local SD Layout

```
/neondrive/
  session_counter.txt          ← monotonic session counter
  sessions/
    <session_id>/
      manifest.json            ← authoritative bundle metadata (required)
      summary.json             ← quick-display status
      events.csv               ← event timeline
      artifacts/
        handshakes.pcap        ← EAPOL handshake capture
        raw.pcap               ← raw monitor-mode capture
        beacon.pcap            ← beacon frame capture
        specter.hc22000        ← hashcat 22000 PMKID/MIC export
        stats.csv              ← per-interval packet statistics
      logs/
        capture_summary.txt    ← human-readable capture summary
        jammit_session.log     ← JAMMIT session log
      sync_complete.flag       ← written last after successful upload
```

---

## Dropbox Layout

```
<cfg.dropbox_folder>/
  neondrive/
    incoming/
      <device_id>/
        sessions/
          <session_id>/
            manifest.json
            summary.json
            events.csv
            artifacts/
            logs/
            sync_complete.flag  ← uploaded last
```

Default `cfg.dropbox_folder` = `/WardriveAnalyzerSync` (configurable at `/keys/config`).

---

## Session ID Format

`ND-<BOARD>-<SEQ>-<MODULE>-T<SECONDS>`

Examples:
- `ND-CYD35-0001-SP3CTER-T0000300`
- `ND-M5TAB5-0003-Y0INK-T0012400`

| Part | Description |
|---|---|
| `ND` | NEONDRIVE prefix |
| `BOARD` | Board code (`CYD35`, `M5TAB5`, `CARDPUTER`, etc.) |
| `SEQ` | Session counter (4-digit, persisted in `/neondrive/session_counter.txt`) |
| `MODULE` | Active capture module at staging time |
| `T<SECONDS>` | Boot-relative seconds (no RTC required) |

The device ID used in the Dropbox path is `ND-<BOARD>-<MAC_SUFFIX>` where
`<MAC_SUFFIX>` is the last 3 bytes of the WiFi MAC address.

---

## Session Bundle Schema

### `manifest.json` (neondrive.session.v1)

```json
{
  "schema": "neondrive.session.v1",
  "session_id": "ND-CYD35-0001-SP3CTER-T0000300",
  "device": {
    "device_id": "ND-CYD35-A91B58",
    "hardware": "NEONDRIVE // CYD-3.5",
    "profile_code": "cyd_3_5"
  },
  "profile": {
    "module": "SP3CTER",
    "mode": "controlled_lab_validation"
  },
  "target": {
    "ssid": "",
    "bssid": "",
    "channel": 0,
    "security": ""
  },
  "timing": {
    "millis_since_boot": 300000
  },
  "artifacts": [
    {"type": "events_csv",  "path": "events.csv",                "required": true},
    {"type": "pcap",        "path": "artifacts/handshakes.pcap", "required": false},
    {"type": "hash_export", "path": "artifacts/specter.hc22000", "format": "hc22000", "required": false}
  ],
  "quick_result": {
    "status": "PASS|WARNING|FAIL|INCONCLUSIVE",
    "findings": []
  }
}
```

### Quick Result Verdicts

| Status | Meaning |
|---|---|
| `PASS` | Module ran, no credential evidence captured |
| `WARNING` | Partial evidence (EAPOL frames but no PMKIDs, or <4 handshake frames) |
| `FAIL` | Strong evidence captured (PMKIDs, or ≥4 EAPOL frames) |
| `INCONCLUSIVE` | No target selected or session not started |

Wardrive Analyzer provides the authoritative scored result after full analysis.

### `events.csv` Columns

```
timestamp_ms,event_type,module,severity,target_ssid,target_bssid,channel,message,artifact_path
```

### `sync_complete.flag`

A small sentinel file uploaded **last** by NEONDRIVE. Wardrive Analyzer must
ignore any session bundle that does not contain this file — it means the upload
is still in progress or was interrupted.

---

## Upload Order

Files are uploaded in this order. If a required file fails, `sync_complete.flag`
is NOT uploaded:

1. `manifest.json` (required)
2. `summary.json`
3. `events.csv`
4. `artifacts/handshakes.pcap`
5. `artifacts/raw.pcap`
6. `artifacts/beacon.pcap`
7. `artifacts/specter.hc22000`
8. `artifacts/stats.csv`
9. `logs/capture_summary.txt`
10. `logs/jammit_session.log`
11. `sync_complete.flag` (last)

---

## Technician Workflow

1. Connect NEONDRIVE to WiFi (needed for Dropbox upload).
2. Run a capture session (Y0INK, SP3CTER, JAMMIT, etc.) against the target AP.
3. Open `http://<device-ip>:8080/analyzer` in a browser.
4. Verify **SD Card: READY** and **Dropbox: CONFIGURED**.
5. Click **Stage Evidence Bundle** — this copies capture files into a clean session folder on SD.
6. Confirm the session ID and Quick Result verdict.
7. Click **Sync to Dropbox** — uploads the bundle; a files-uploaded count confirms success.
8. On the PC, let Dropbox sync complete.
9. In Wardrive Analyzer, go to **SD Ingest** tab.
10. Click **Import NEONDRIVE Session…** and select the synced session folder.
11. Run analysis from the Evidence Vault.

---

## Troubleshooting

| Symptom | Check |
|---|---|
| "Dropbox token not configured" | Go to `/keys/config`, add Dropbox token |
| "SD not available" | SD card not mounted; reboot device |
| Stage produces empty session | Run a capture module first (Y0INK, SP3CTER, etc.) |
| Sync fails with HTTP 401 | Dropbox token expired; regenerate at dropbox.com/developers |
| Sync fails with HTTP 409 | Dropbox conflict; retry — `mode=overwrite` should handle it |
| Wardrive Analyzer rejects session | Check `sync_complete.flag` is present in the Dropbox-synced folder |
| Device ID shows `ND--` | WiFi not initialized before `deviceId()` called — check WiFi state |

---

## Security Notes

- `manifest.json` must not contain Dropbox tokens, WiFi passwords, or API keys.
- Target SSID/BSSID/channel are acceptable evidence metadata.
- Hash exports (`.hc22000`) and PCAPs contain evidence of a controlled lab
  authorization test and should be treated as sensitive lab artifacts.
- Wardrive Analyzer never executes files from the imported bundle.
- Path traversal in artifact paths is rejected by the importer.

---

## Future: Bidirectional Writeback (v2)

Wardrive Analyzer could write back to the session folder:
```
sessions/<session_id>/
  analysis.json       ← scored result from Wardrive Analyzer
  report.html         ← full HTML report
  remediation.json    ← recommended remediation steps
```

The schema already reserves `sync.analyzer_status` in `summary.json` for this
handshake. NEONDRIVE could poll for these files and display the scored result
on the `/analyzer` page. This is out of scope for v1.
