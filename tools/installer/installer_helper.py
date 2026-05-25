#!/usr/bin/env python3
"""
NEONDRIVE installer helper — called by installer.bat / install.sh.

Subcommands:
  bootstrap          Create .installer/venv and install esptool + pyserial.
                     Fast (~10 s); does NOT install PlatformIO.
  list-ports         Print connected serial ports as "DEVICE|Description" lines.
  flash-bin          Flash a precompiled bin from Device-Bins/ via esptool.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


# ── Helpers ───────────────────────────────────────────────────────────────────

def run(cmd: list[str], cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    proc = subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)


def ensure_venv(root: Path) -> Path:
    """Create the .installer/venv if it does not exist; return path to python."""
    venv_dir = root / ".installer" / "venv"
    if sys.platform == "win32":
        py = venv_dir / "Scripts" / "python.exe"
    else:
        py = venv_dir / "bin" / "python"
    if not py.exists():
        run([sys.executable, "-m", "venv", str(venv_dir)], cwd=root)
    return py


# ── Commands ──────────────────────────────────────────────────────────────────

def bootstrap(root: Path) -> None:
    """
    Set up the installer venv with esptool and pyserial.

    Intentionally minimal — PlatformIO is NOT installed here.
    Quick Install uses precompiled bins so only esptool + pyserial are needed.
    Total download: ~15 MB, runs in ~10 seconds on a fast connection.
    """
    py = ensure_venv(root)
    pip_base = [str(py), "-m", "pip", "--disable-pip-version-check"]
    run([*pip_base, "install", "--upgrade", "pip", "--quiet"])
    run([*pip_base, "install", "esptool==5.2.0", "pyserial", "--quiet"])


def list_ports() -> None:
    """
    Print one line per detected serial port: DEVICE|Description
    The | delimiter is replaced with / in the description so the bat parser
    (which uses | as a field separator) stays unambiguous.
    """
    try:
        from serial.tools import list_ports as lp
    except ImportError:
        # pyserial not yet installed (called before bootstrap) — print nothing
        return

    ports = sorted(lp.comports(), key=lambda p: p.device)
    for p in ports:
        desc = (p.description or "Unknown device").replace("|", "/").strip()
        print(f"{p.device}|{desc}")


def find_board(manifest: dict, board_name: str) -> dict:
    for b in manifest.get("boards", []):
        if b.get("name") == board_name:
            return b
    known = [b.get("name") for b in manifest.get("boards", [])]
    raise SystemExit(
        f"Board '{board_name}' not found in installer manifest.\n"
        f"Available boards: {', '.join(known)}"
    )


def flash_bin(root: Path, board_name: str, port: str) -> None:
    """Flash a precompiled fullflash binary from Device-Bins/."""
    manifest_path = root / "Device-Bins" / "installer_manifest.json"
    if not manifest_path.exists():
        raise SystemExit(
            f"Missing: {manifest_path}\n"
            "Re-download the release package or run: python scripts/build_device_bins.py"
        )

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    board = find_board(manifest, board_name)

    bin_path = root / "Device-Bins" / board["file"]
    if not bin_path.exists():
        raise SystemExit(
            f"Missing binary: {bin_path.name}\n"
            "The Device-Bins folder may be incomplete."
        )

    chip = board["chip"]
    offset = board.get("fullflash_offset", "0x0")

    # Baud rates tuned per chip family
    baud_map = {
        "esp32p4":  "1500000",
        "esp32s3":  "921600",
        "esp32s2":  "460800",
        "esp32c3":  "460800",
        "esp32":    "460800",
    }
    baud = baud_map.get(chip, "460800")

    env = os.environ.copy()
    env["PYTHONUTF8"] = "1"

    cmd = [
        sys.executable, "-m", "esptool",
        "--chip", chip,
        "--port", port,
        "--baud", baud,
        "write_flash", offset, str(bin_path),
    ]
    run(cmd, env=env)


# ── Entry point ───────────────────────────────────────────────────────────────

def main() -> int:
    parser = argparse.ArgumentParser(
        description="NEONDRIVE installer helper",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("bootstrap",  help="Create venv and install esptool + pyserial")
    sub.add_parser("list-ports", help="List connected serial ports")

    p_flash = sub.add_parser("flash-bin", help="Flash a precompiled bin")
    p_flash.add_argument("--board", required=True, help="Board name (matches installer manifest)")
    p_flash.add_argument("--port",  required=True, help="Serial port (e.g. COM3 or /dev/ttyUSB0)")

    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]

    if args.cmd == "bootstrap":
        bootstrap(root)
    elif args.cmd == "list-ports":
        list_ports()
    elif args.cmd == "flash-bin":
        flash_bin(root, args.board, args.port)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
