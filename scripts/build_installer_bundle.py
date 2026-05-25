#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path


def run(cmd: list[str], cwd: Path) -> None:
    print("[installer-bundle]", " ".join(cmd))
    rc = subprocess.run(cmd, cwd=str(cwd)).returncode
    if rc != 0:
        raise SystemExit(rc)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Build NEONDRIVE installer bundle zip")
    p.add_argument("--version", default="", help="Version override")
    p.add_argument("--out-dir", default="dist/installers", help="Output directory")
    p.add_argument("--no-clean", action="store_true", help="Skip clean build")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parents[1]

    cmd = [sys.executable, str(root / "scripts" / "build_device_bins.py")]
    if args.version:
        cmd += ["--version", args.version]
    if args.no_clean:
        cmd.append("--no-clean")
    run(cmd, root)

    manifest = root / "Device-Bins" / "installer_manifest.json"
    if not manifest.exists():
        raise SystemExit("Missing Device-Bins/installer_manifest.json")
    import json
    data = json.loads(manifest.read_text(encoding="utf-8"))
    version = data["version"]

    stage = root / ".installer_stage" / version
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True, exist_ok=True)

    shutil.copy2(root / "installer.bat", stage / "installer.bat")
    sh_src = root / "install.sh"
    if sh_src.exists():
        shutil.copy2(sh_src, stage / "install.sh")
    shutil.copytree(root / "Device-Bins", stage / "Device-Bins", dirs_exist_ok=True)
    # Copy installer tools but skip __pycache__ to keep the zip clean
    tools_dst = stage / "tools" / "installer"
    tools_dst.mkdir(parents=True, exist_ok=True)
    for src_file in (root / "tools" / "installer").iterdir():
        if src_file.name != "__pycache__" and src_file.is_file():
            shutil.copy2(src_file, tools_dst / src_file.name)

    readme = stage / "README_Installer.txt"
    readme.write_text(
        "NEONDRIVE Installer\n"
        "===================\n\n"
        "Windows:\n"
        "  1) Connect your board by USB\n"
        "  2) Double-click installer.bat\n"
        "  3) Pick your hardware and COM port\n"
        "  4) Follow any on-screen boot-mode instructions\n\n"
        "macOS / Linux:\n"
        "  1) Connect your board by USB\n"
        "  2) Run:  bash install.sh\n"
        "  3) Pick your hardware and serial port\n\n"
        "Requirements:\n"
        "  Python 3.10+ must be installed (https://python.org).\n"
        "  The first run downloads esptool and pyserial (~15 MB).\n",
        encoding="utf-8",
    )

    out_dir = (root / args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    zip_path = out_dir / f"NEONDRIVE_Installer_{version}.zip"
    if zip_path.exists():
        zip_path.unlink()

    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for p in stage.rglob("*"):
            if p.is_file():
                zf.write(p, p.relative_to(stage))

    print(f"[installer-bundle] wrote {zip_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
