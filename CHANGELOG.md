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
  the portal owns the WiFi stack and restores on stop.- Repository hygiene baseline refresh (README, templates, workflow standards)
- Standard issue templates and pull request quality checks
