# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog and follows Semantic Versioning.

## [Unreleased]

### Added
- **G1 — Evil Portal (B1)**: captive-portal assessment module (AP + DNS + HTTP
  on the device) with credential-hygiene design — submitted credential values
  are never persisted to SD or logs; only timestamped submission events and
  counts enter the session bundle, which auto-stages on portal stop and feeds
  the quick-verdict. Web UI at `/portal` + API; management UI suspends while
  the portal owns the WiFi stack and restores on stop.
- **G2 — Sub-GHz toolkit (B2)**: CC1101 support on the T-Embed CC1101 Plus
  target (RadioLib, 315/434/868/915 MHz) — spectrum scan, packet capture,
  replay, and duty-capped jammer. Captures are staged into the session bundle
  (`artifacts/`, `events.csv`) with wall-clock timestamps. No-op stubs on all
  other targets.
- Repository hygiene baseline refresh (README, templates, workflow standards)
- Standard issue templates and pull request quality checks
