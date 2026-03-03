# NEONDRIVE

NEONDRIVE is a multi-target ESP32 firmware project for CYD and select LilyGO boards. It's built for hands-on hardware hackers: people who like to tinker, improve, and share.

This project grew from a love of hacker culture — the curiosity, the late-night debugging, and the generous sharing of ideas. Special thanks and inspiration to the Marauder project, M5porkchop, and Bruce for lighting the way.

What you'll find here:

- Firmware sources for CYD 2.4 / 2.8 / 3.5 and select LilyGO devices
- PlatformIO build environments and helper scripts
- Release packaging that produces friendly `Device-Bins` for users

---

**Quick Start**

- Flash a prebuilt image from `Device-Bins/` or the Releases page.
- Build locally with PlatformIO (example):

```powershell
python -m pip install -U platformio
python -m platformio run -e firmware_cyd_3_5
python -m platformio run -e firmware_cyd_3_5 -t upload --upload-port <PORT>
```

Replace `<PORT>` with your serial port (Windows `COMx`, Linux `/dev/ttyUSBx`, macOS `/dev/cu.*`). Use `python -m platformio device list` to discover ports.

---

## Why this project feels "hacker"

NEONDRIVE emphasizes reproducible, tweakable firmware over polished, opaque binaries. We favor:

- Clear build steps so anyone can reproduce a firmware image.
- Small, focused changes with well-described commits.
- Sharing credit — many ideas here were inspired by Marauder, M5porkchop, and Bruce.

If that sounds like your kind of project, jump in.

---

## Supported Hardware (overview)

See `docs/HARDWARE_TARGETS.md` for full details. Primary targets include:

- CYD 2.4 / CYD 2.8 / CYD 3.5 (ESP32 variants)
- LilyGO T-Display-S3 (ESP32-S3)
- LilyGO T-Embed CC1101 (ESP32-S3 + radio)

## Workflow — build, test, release

1. Setup

   - Install PlatformIO and toolchain as in `docs/INSTALL.md`.

2. Build

   - Build a single target:

   ```powershell
   python -m platformio run -e firmware_cyd_3_5
   ```

   - Build and flash in one step:

   ```powershell
   python -m platformio run -e firmware_cyd_3_5 -t upload --upload-port <PORT>
   ```

3. Test

   - Monitor serial output:

   ```powershell
   python -m platformio device monitor -p <PORT> -b 115200
   ```

4. Release

   - Use helper scripts to produce release artifacts:

   ```powershell
   python scripts/build_release_bins.py --version vX.Y.Z
   python scripts/build_device_bins.py --version vX.Y.Z
   ```

---

## Branching & Contributions

- Use feature branches and open PRs against `main`.
- Keep commits focused and include clear descriptions.
- Open an issue before larger changes (new hardware, major refactors) so we can coordinate.

See `CONTRIBUTING.md` for details.

---

## Troubleshooting (short)

- Use a data-capable USB cable.
- If flashing fails, try holding BOOT or toggling RESET during upload.
- If CYD 3.5 touch is off, remove the SD calibration file (`/touch_cal_cyd35.json`) and reboot.

---

## License & Credits

This project is licensed under `LICENSE` in this repo.

Thanks to the broader maker community for inspiration — especially Marauder, M5porkchop, and Bruce.

---

See also:

- [docs/INSTALL.md](docs/INSTALL.md)
- [docs/HARDWARE_TARGETS.md](docs/HARDWARE_TARGETS.md)
- [Device-Bins/](Device-Bins/)
