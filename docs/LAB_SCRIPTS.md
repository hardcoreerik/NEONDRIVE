# Lab Procedure Scripts (G6)

A minimal **line-based macro runner** for repeatable lab procedures.
Deliberately not a JS interpreter — classic ESP32 RAM is tight, and lab
procedures need auditability, not a programming language.

## Script files

On SD: `/neondrive/scripts/*.ndm`. One command per line, `#` comments,
double-quoted args may contain spaces. Listed/run from the web UI at
`/scripts` (`GET /api/scripts/list`, `POST /api/scripts/run`,
`POST /api/scripts/stop`, `GET /api/scripts/status`).

## Commands

| Command | Args | Effect |
|---|---|---|
| `scan` | — | Blocking AP scan; fills the AP list for `target` |
| `target` | `<ssid> [bssid] [channel]` | Lock the working target (prefers a scanned AP entry) |
| `capture` | `<karma\|deauth\|beacon\|probe> <seconds>` | Time-bounded Packet Lab attack (non-blocking; pair with `delay`) |
| `karma start` | `<channel> <seconds>` | Probe-response spoofing (channel 1–14, 5–300 s) |
| `karma stop` | — | Stop Karma attack |
| `portal start` | `<ssid> <channel> <duration_s> [kicker]` | Evil portal (auto-stages on stop) |
| `portal stop` | — | Stop portal |
| `subghz scan|rx|replay|jam|stop` | `[freq_mhz]` | T-Embed sub-GHz toolkit |
| `stage` | — | Stage the session bundle |
| `sync` | — | Upload staged bundle (needs Dropbox token) |
| `verdict` | — | Print session manifest to serial |
| `log` | `<free text…>` | Annotate `events.csv` |
| `delay` | `<seconds>` | Non-blocking wait (max 3600 s) |

## Example

```ndm
# LP-001 — baseline survey, lab bench only
scan
target "CorpNet"
capture karma 30
delay 35
portal start "CorpNet" 6 120
delay 130
portal stop
stage
sync
```

## Semantics

- Execution is **non-blocking**: `ScriptRunner::tick()` advances one step per
  `loop()`; `delay` waits without stalling the device.
- Every executed step is logged to the staged session's `events.csv`
  (`script_step` / `script_step_failed`) and to serial.
- A **failing step aborts the run** — lab procedures must not silently skip.
- Scripts are capped at 8 KB. Only files under `/neondrive/scripts/` can run
  (path traversal is rejected).
- Review any `.ndm` before running it — scripts can start portals, jammers,
  and uploads. Do not run scripts from untrusted sources.
