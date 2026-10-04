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

