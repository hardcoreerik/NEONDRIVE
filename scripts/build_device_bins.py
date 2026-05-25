#!/usr/bin/env python3
"""
Build and package one-click fullflash bins into Device-Bins/.

Output naming:
  NEONDRIVE_<DEVICE_NAME>_<VERSION>.bin
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path


TARGETS = (
    ("cyd_2_4",        "CYD-2.4",        "esp32",    True),
    ("cyd_2_8",        "CYD-2.8",        "esp32",    True),
    ("cyd_3_5",        "CYD-3.5",        "esp32",    True),
    ("m5tab5",         "M5Tab5",         "esp32p4",  True),
    ("t_display_s3",   "T-DisplayS3",    "esp32s3",  False),
    ("t_embed_cc1101", "T-Embed-CC1101", "esp32s3",  False),
    ("cardputer_adv",  "Cardputer-Adv",  "esp32s3",  False),
)
# (env_key, device_name, chip, stable)


def log(msg: str) -> None:
    print(f"[device-bins] {msg}")


def run(cmd: list[str], cwd: Path) -> None:
    log(" ".join(cmd))
    result = subprocess.run(cmd, cwd=str(cwd))
    if result.returncode != 0:
        raise RuntimeError(f"Command failed ({result.returncode}): {' '.join(cmd)}")


def sanitize_version(raw: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "_", raw).strip("._-")


def infer_version(project_root: Path) -> str:
    try:
        completed = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=str(project_root),
            capture_output=True,
            text=True,
            check=True,
        )
        candidate = sanitize_version(completed.stdout.strip())
        if candidate:
            return candidate
    except Exception:
        pass

    return dt.datetime.now(dt.UTC).strftime("dev-%Y%m%d-%H%M%S")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build Device-Bins package.")
    parser.add_argument(
        "--version",
        default="",
        help="Version text used in filenames (default: git describe or UTC timestamp).",
    )
    parser.add_argument(
        "--out-dir",
        default="Device-Bins",
        help="Output directory for packaged bins (default: Device-Bins).",
    )
    parser.add_argument(
        "--no-clean",
        action="store_true",
        help="Skip clean build in release-bin generation.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    project_root = Path(__file__).resolve().parent.parent
    version = sanitize_version(args.version) if args.version else infer_version(project_root)
    if not version:
        raise ValueError("Version resolved to an empty string.")

    targets_csv = ",".join([key for key, *_ in TARGETS])
    release_cmd = [
        sys.executable,
        str(project_root / "scripts" / "build_release_bins.py"),
        "--version",
        version,
        "--targets",
        targets_csv,
    ]
    if args.no_clean:
        release_cmd.append("--no-clean")
    run(release_cmd, project_root)

    release_dir = project_root / "dist" / "releases" / version
    out_dir = (project_root / args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    manifest = {
        "project": "NEONDRIVE",
        "version": version,
        "boards": [],
    }
    for target_key, device_name, chip, stable in TARGETS:
        src = release_dir / f"neondrive_{version}_{target_key}_fullflash.bin"
        if not src.exists():
            raise FileNotFoundError(f"Missing expected fullflash binary: {src}")
        filename = f"NEONDRIVE_{device_name}_{version}.bin"
        dst = out_dir / filename
        shutil.copy2(src, dst)
        log(f"wrote {dst.name}")
        manifest["boards"].append({
            "name": device_name,
            "file": filename,
            "chip": chip,
            "env": f"firmware_{target_key}",
            "stable": stable,
            "fullflash_offset": "0x0",
        })

    manifest_path = out_dir / "installer_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    log(f"wrote {manifest_path.name}")

    readme = out_dir / "README.md"
    readme.write_text(
        "\n".join(
            [
                "# Device-Bins",
                "",
                "These are fullflash binaries (flash at offset `0x0`).",
                "",
                "Filename format:",
                "- `NEONDRIVE_<DEVICE_NAME>_<VERSION>.bin`",
                "",
                "Example flash command (ESP32 boards):",
                "- `python -m platformio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32 --port COM15 --baud 460800 write_flash 0x0 NEONDRIVE_CYD-3.5_<VERSION>.bin`",
                "Example flash command (ESP32-S3 boards):",
                "- `python -m platformio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port COM3 --baud 460800 write_flash 0x0 NEONDRIVE_T-DisplayS3_<VERSION>.bin`",
                "Example flash command (ESP32-P4 Tab5):",
                "- `python -m esptool --chip esp32p4 --port COM4 --baud 1500000 write_flash 0x0 NEONDRIVE_M5Tab5_<VERSION>.bin`",
                "",
                "CYD-3.5 note:",
                "- First boot runs touch calibration and saves `/touch_cal_cyd35.json` to SD card.",
                "- To recalibrate, delete `/touch_cal_cyd35.json` from SD and reboot.",
                "",
            ]
        ),
        encoding="utf-8",
        newline="\n",
    )
    log(f"wrote {readme.name}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[device-bins] ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
