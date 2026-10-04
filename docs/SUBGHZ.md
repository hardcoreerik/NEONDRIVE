# Sub-GHz Toolkit (G2)

CC1101 sub-GHz support for the **LilyGO T-Embed CC1101 Plus** target only.
Clean-room implementation on RadioLib 6.5.0 (t_embed env lib_dep) — no
Marauder/Bruce/GhostESP code.

## Wiring / SPI coordination

The CC1101 shares the HSPI bus with the display/touch:

| Signal | GPIO |
|---|---|
| SCK / MISO / MOSI | 11 / 10 / 9 |
| CS (CC1101) | 12 |
| GDO0 / GDO2 | 3 / 38 |
| Antenna SW1 / SW0 | 47 / 48 |

The module uses the **SPI-parameter RadioLib `Module` constructor** (it does
not re-begin the bus), asserts only its own CS line, and sets the antenna
switch per band (315 MHz: H/L, 434 MHz: H/H, 868/915 MHz: L/H). All other
targets compile with no-op stubs, so `firmware_cyd_*` / Tab5 builds are
unaffected.

## Features (web UI `/subghz` or scripts)

- **Scan** 430–440 MHz (0.2 MHz steps) — RSSI peak hunt.
- **RX capture** on a chosen frequency — packets land in the session bundle:
  `artifacts/subghz_<freq>_<seq>.bin` (raw bytes) + `events.csv` with
  wall-clock timestamps, RSSI, and byte counts.
- **Replay** — retransmit the last capture N times.
- **Jam** — CW carrier on a frequency, time-bounded (10 s from the web UI).

## Regulatory note

Transmit only on your own devices in a lab setting. The 315/434/868/915 MHz
bands carry duty-cycle and power limits in most jurisdictions — see
[SECURITY.md](../SECURITY.md).
