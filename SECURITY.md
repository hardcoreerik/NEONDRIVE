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
