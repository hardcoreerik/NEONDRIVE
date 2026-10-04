# Evil Portal (G1)

Captive-portal assessment module for NEONDRIVE. Clean-room implementation —
no Marauder/Bruce/GhostESP code.

## What it does

Starts an open AP + DNS catch-all + HTTP server (port 80) on the device and
serves a lab-branded login form. When a client connects and submits the form,
the portal **does not record what was typed** — it records only:

- a timestamped `portal_cred_submit` event in the staged session's
  `events.csv` (via the `AnalyzerSync::appendEvent` API),
- an incremented per-portal submission counter, which feeds the session
  quick-verdict as a **FAIL** when > 0.

This keeps the module on the assessment-evidence side: the lab can answer
"did clients submit credentials?" without ever holding the credentials.

## Lifecycle

1. Web UI `/portal` or `portal start <ssid> <channel> <duration_s> [kicker]`.
2. The management web UI (port 8080) is suspended while the portal owns the
   WiFi stack; the portal runs its own server on port 80.
3. On stop (manual, duration timeout, or `/api/portal/stop`) the session
   bundle is **auto-staged** (session id `portal-<epoch>`) and the management
   UI is restored.

## Options

| Param | Default | Notes |
|---|---|---|
| ssid | `LAB-PORTAL` (or current target SSID) | Lab-scoped default |
| channel | 6 (or current target channel) | |
| duration_s | 0 = until stopped | Script runs may cap this |
| deauthKicker | off | Optional raw-TX nudge onto the portal for the locked target. Same TX guard pattern as Karma: no-op on M5TAB5/M5CARDPUTER. |

## Authorized-use framing

Run only against SSIDs you own or are authorised to test. The served page
carries a lab-authorisation notice. Credential values are never persisted —
see [SECURITY.md](../SECURITY.md).
