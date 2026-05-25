#!/usr/bin/env python3
"""
Build release binaries for all supported hardware targets.

Artifacts per target:
  - neondrive_<version>_<target>_app.bin
      Flash at 0x10000 (upgrade path)
  - neondrive_<version>_<target>_fullflash.bin
      Flash at 0x0 (fresh install path)

Also generates:
  - release_manifest.json
  - release_sha256.txt
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List


@dataclass(frozen=True)
class TargetConfig:
    key: str
    env: str
    label: str
    chip: str
    flash_size: str
    bootloader_offset: str


TARGETS = {
    "cyd_2_4": TargetConfig(
        key="cyd_2_4",
        env="firmware_cyd_2_4",
        label="CYD 2.4",
        chip="esp32",
        flash_size="4MB",
        bootloader_offset="0x1000",
    ),
    "cyd_2_8": TargetConfig(
        key="cyd_2_8",
        env="firmware_cyd_2_8",
        label="CYD 2.8",
        chip="esp32",
        flash_size="4MB",
        bootloader_offset="0x1000",
    ),
    "cyd_3_5": TargetConfig(
        key="cyd_3_5",
        env="firmware_cyd_3_5",
        label="CYD 3.5",
        chip="esp32",
        flash_size="4MB",
        bootloader_offset="0x1000",
    ),
    "m5tab5": TargetConfig(
        key="m5tab5",
        env="firmware_m5tab5",
        label="M5Stack Tab5",
        chip="esp32p4",
        flash_size="16MB",
        bootloader_offset="0x2000",
    ),
    "t_display_s3": TargetConfig(
        key="t_display_s3",
        env="firmware_t_display_s3",
        label="LilyGO T-Display-S3",
        chip="esp32s3",
        flash_size="16MB",
        bootloader_offset="0x0000",
    ),
    "t_embed_cc1101": TargetConfig(
        key="t_embed_cc1101",
        env="firmware_t_embed_cc1101",
        label="LilyGO T-Embed CC1101",
        chip="esp32s3",
        flash_size="16MB",
        bootloader_offset="0x0000",
    ),
    "cardputer_adv": TargetConfig(
        key="cardputer_adv",
        env="firmware_cardputer_adv",
        label="M5Stack Cardputer Advanced",
        chip="esp32s3",
        flash_size="8MB",
        bootloader_offset="0x0000",
    ),
}


def log(msg: str) -> None:
    print(f"[release] {msg}")


def run(cmd: List[str], cwd: Path) -> None:
    log(" ".join(cmd))
    result = subprocess.run(cmd, cwd=str(cwd))
    if result.returncode != 0:
        raise RuntimeError(f"Command failed ({result.returncode}): {' '.join(cmd)}")


def pio_cmd(*args: str) -> List[str]:
    return [sys.executable, "-m", "platformio", *args]


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


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def boot_app0_candidates() -> Iterable[Path]:
    core_env = os.environ.get("PLATFORMIO_CORE_DIR")
    roots = []
    if core_env:
        roots.append(Path(core_env))
    roots.append(Path.home() / ".platformio")

    for root in roots:
        packages_dir = root / "packages"
        if not packages_dir.exists():
            continue
        for candidate in sorted(packages_dir.glob("framework-arduinoespressif32*/tools/partitions/boot_app0.bin")):
            yield candidate


def find_boot_app0() -> Path:
    for candidate in boot_app0_candidates():
        if candidate.exists():
            return candidate
    raise FileNotFoundError(
        "Could not locate boot_app0.bin under PlatformIO packages. "
        "Build once with PlatformIO or verify PLATFORMIO_CORE_DIR."
    )


def build_target(project_root: Path, target: TargetConfig, clean: bool) -> Path:
    if clean:
        run(pio_cmd("run", "-e", target.env, "-t", "clean"), project_root)
    run(pio_cmd("run", "-e", target.env), project_root)
    build_dir = project_root / ".pio" / "build" / target.env
    if not build_dir.exists():
        raise FileNotFoundError(f"Build directory missing: {build_dir}")
    return build_dir


def merge_fullflash(
    project_root: Path,
    target: TargetConfig,
    build_dir: Path,
    boot_app0: Path,
    output_bin: Path,
) -> None:
    bootloader = build_dir / "bootloader.bin"
    partitions = build_dir / "partitions.bin"
    firmware = build_dir / "firmware.bin"

    for required in (bootloader, partitions, firmware, boot_app0):
        if not required.exists():
            raise FileNotFoundError(f"Required binary missing: {required}")

    merge = pio_cmd(
        "pkg",
        "exec",
        "-p",
        "tool-esptoolpy",
        "--",
        "esptool.py",
        "--chip",
        target.chip,
        "merge_bin",
        "-o",
        str(output_bin),
        "--flash_mode",
        "dio",
        "--flash_size",
        target.flash_size,
        target.bootloader_offset,
        str(bootloader),
        "0x8000",
        str(partitions),
        "0xe000",
        str(boot_app0),
        "0x10000",
        str(firmware),
    )
    run(merge, project_root)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build NEONdrive release bins.")
    parser.add_argument(
        "--version",
        default="",
        help="Release version used in artifact names (default: git describe or UTC timestamp).",
    )
    parser.add_argument(
        "--targets",
        default="all",
        help="Comma-separated targets: cyd_2_4,cyd_3_5,t_display_s3,t_embed_cc1101 or 'all'.",
    )
    parser.add_argument(
        "--out-dir",
        default="dist/releases",
        help="Base output directory (default: dist/releases).",
    )
    parser.add_argument(
        "--no-clean",
        action="store_true",
        help="Skip 'pio run -t clean' before building each target.",
    )
    return parser.parse_args()


def resolve_targets(raw: str) -> List[TargetConfig]:
    if raw == "all":
        return [
            TARGETS["cyd_2_4"],
            TARGETS["cyd_2_8"],
            TARGETS["cyd_3_5"],
            TARGETS["m5tab5"],
            TARGETS["t_display_s3"],
            TARGETS["t_embed_cc1101"],
            TARGETS["cardputer_adv"],
        ]

    keys = [item.strip() for item in raw.split(",") if item.strip()]
    selected = []
    for key in keys:
        if key not in TARGETS:
            valid = ", ".join(TARGETS.keys())
            raise ValueError(f"Unknown target '{key}'. Valid targets: {valid}")
        selected.append(TARGETS[key])
    if not selected:
        raise ValueError("No targets selected.")
    return selected


def main() -> int:
    args = parse_args()
    project_root = Path(__file__).resolve().parent.parent

    version = sanitize_version(args.version) if args.version else infer_version(project_root)
    if not version:
        raise ValueError("Version resolved to an empty string.")

    targets = resolve_targets(args.targets.lower())
    release_dir = (project_root / args.out_dir / version).resolve()
    release_dir.mkdir(parents=True, exist_ok=True)

    boot_app0 = find_boot_app0()
    log(f"boot_app0: {boot_app0}")
    log(f"version: {version}")
    log(f"release dir: {release_dir}")

    manifest = {
        "project": "NEONdrive",
        "version": version,
        "generated_utc": dt.datetime.now(dt.UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "targets": [],
    }
    checksum_entries = []

    for target in targets:
        log(f"Building target: {target.key} ({target.label})")
        build_dir = build_target(project_root, target, clean=not args.no_clean)

        firmware = build_dir / "firmware.bin"
        app_name = f"neondrive_{version}_{target.key}_app.bin"
        full_name = f"neondrive_{version}_{target.key}_fullflash.bin"
        app_dst = release_dir / app_name
        full_dst = release_dir / full_name

        shutil.copy2(firmware, app_dst)
        merge_fullflash(project_root, target, build_dir, boot_app0, full_dst)

        app_hash = sha256_file(app_dst)
        full_hash = sha256_file(full_dst)

        checksum_entries.append((app_hash, app_name))
        checksum_entries.append((full_hash, full_name))

        manifest["targets"].append(
            {
                "target": target.key,
                "label": target.label,
                "env": target.env,
                "chip": target.chip,
                "flash_size": target.flash_size,
                "artifacts": {
                    "app_bin": app_name,
                    "fullflash_bin": full_name,
                },
                "flash_map": [
                    {"offset": target.bootloader_offset, "file": "bootloader.bin"},
                    {"offset": "0x8000", "file": "partitions.bin"},
                    {"offset": "0xE000", "file": "boot_app0.bin"},
                    {"offset": "0x10000", "file": "firmware.bin"},
                ],
            }
        )

    checksum_path = release_dir / "release_sha256.txt"
    with checksum_path.open("w", encoding="utf-8", newline="\n") as fh:
        for digest, filename in checksum_entries:
            fh.write(f"{digest} *{filename}\n")

    manifest_path = release_dir / "release_manifest.json"
    with manifest_path.open("w", encoding="utf-8", newline="\n") as fh:
        json.dump(manifest, fh, indent=2)
        fh.write("\n")

    log("Release artifacts ready:")
    for _, filename in checksum_entries:
        log(f"  - {filename}")
    log(f"  - {checksum_path.name}")
    log(f"  - {manifest_path.name}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[release] ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
