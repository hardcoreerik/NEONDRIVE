# Security Policy

## Responsible Use Disclaimer

NEONDRIVE firmware includes WiFi scanning, frame injection (deauthentication),
passive capture (PMKID / handshakes), and network reconnaissance tools.

**These capabilities are provided for:**
- Authorised penetration testing of networks you own or have explicit written permission to test
- Security research in controlled/lab environments
- Personal home network hardening and awareness

**Use on networks you do not own or have permission to test is illegal in most jurisdictions,**
including but not limited to offences under the Computer Fraud and Abuse Act (US),
the Computer Misuse Act (UK), and equivalent laws worldwide.

By building, flashing, or using this firmware you accept full legal and ethical
responsibility for how it is used.

---

## New Modules — Authorised-Use Notes (2026-10)

### Evil Portal (credential hygiene)

The portal is a **credential assessment aid**, not a credential harvester:

- Submitted credential values are **never written to SD or serial logs**.
- Only timestamped *submission events* (client MAC, time) and running
  submission **counts** enter the session bundle, and the counts feed the
  quick-verdict. No replayable secret leaves the device.
- Run only against SSIDs you own or are authorised to test. The captive-portal
  page must carry the lab-authorisation notice template.
- The portal SSID defaults to `LAB-PORTAL` and channel selection prefers the
  currently locked target — lab-scoped defaults, no public-nuisance presets.

### Sub-GHz toolkit (CC1101)

- Transmit functions (`replay`, `jam`) are intended for **your own devices in
  a shielded/lab setting**. Check local radio regulations before transmitting
  on 315/434/868/915 MHz — many bands have duty-cycle and power limits.
- The jammer is capped at 10 s per activation from the web UI (configurable
  per call in scripts, still time-bounded).

### Live capture stream

- The TCP pcap stream carries raw 802.11 frames — treat the listening network
  as untrusted. No authentication is provided; keep it on a lab network and
  stop capture when not actively analysing.

### Lab scripts

- Scripts execute device operations unattended. Review any `.ndm` file before
  running it, and note that a failing step **aborts the run** (by design —
  lab procedures must not silently skip steps).
- Do not accept `.ndm` files from untrusted sources; they can start portals,
  jammers, and uploads.

## Reporting a Vulnerability

If you discover a security vulnerability in this project (e.g. a flaw that could
allow credential leakage, unintended remote access, or unsafe default config), please
**do not open a public GitHub Issue**.

Instead:

1. Email the maintainer at the address listed in [.github/FUNDING.yml](.github/FUNDING.yml),
   or open a [GitHub Security Advisory](../../security/advisories/new) on this repo.
2. Include a description of the issue, steps to reproduce, and impact assessment.
3. Allow up to 14 days for an initial response before any public disclosure.

We aim to acknowledge all valid reports and publish a fix or mitigation within 30 days.

---

## Scope

| In scope | Out of scope |
|---|---|
| Firmware credential handling (config.json, API keys at rest) | Vulnerabilities in third-party libraries (report upstream) |
| Unintended network exposure via AP mode or web UI | Social engineering |
| Config parsing / injection flaws | Physical hardware attacks |
| Companion app (CYDCompanion) data handling | Expected offensive behaviour (that is the feature) |

---

## Supported Versions

Security fixes will be applied to the latest release only.
Older release binaries in `Device-Bins/` or GitHub Releases are not backported.
