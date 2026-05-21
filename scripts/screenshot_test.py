#!/usr/bin/env python3
import argparse
import json
import os
import sys
import time
from pathlib import Path

try:
    import serial
except Exception:
    print("pyserial is required: pip install pyserial", file=sys.stderr)
    raise


def rpc(ser, obj, timeout=8.0):
    rid = obj.get("id", 1)
    line = json.dumps(obj, separators=(",", ":")) + "\n"
    ser.write(line.encode("utf-8"))
    ser.flush()
    deadline = time.time() + timeout
    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        try:
            msg = json.loads(raw.decode("utf-8", errors="ignore").strip())
        except Exception:
            continue
        if msg.get("id") == rid:
            return msg
    raise TimeoutError(f"RPC timeout waiting for id={rid}")


def next_id(state=[0]):
    state[0] += 1
    return state[0]


def download_file(ser, sd_path, out_path):
    msg = rpc(ser, {"id": next_id(), "cmd": "download_begin", "path": sd_path})
    if not msg.get("ok"):
        raise RuntimeError(f"download_begin failed: {msg}")
    token = msg["token"]
    total = int(msg.get("size", 0))
    offset = 0
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        while True:
            ck = rpc(ser, {
                "id": next_id(), "cmd": "download_chunk", "token": token,
                "offset": offset, "max": 512
            })
            if not ck.get("ok"):
                raise RuntimeError(f"download_chunk failed: {ck}")
            data = bytes.fromhex(ck.get("hex", ""))
            f.write(data)
            offset += len(data)
            if ck.get("eof", False):
                break
    rpc(ser, {"id": next_id(), "cmd": "download_end", "token": token})
    return total, offset


def main():
    ap = argparse.ArgumentParser(description="Trigger firmware screenshot and download from SD")
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", default="D:/AI/images/latest_device_screenshot.bmp")
    ap.add_argument("--wait", type=float, default=3.0, help="seconds to wait after opening serial")
    args = ap.parse_args()

    out_path = Path(args.out)

    with serial.Serial(args.port, args.baud, timeout=0.4) as ser:
        time.sleep(args.wait)
        ser.reset_input_buffer()

        # Request screenshot capture to SD.
        res = rpc(ser, {
            "id": next_id(),
            "cmd": "control",
            "name": "SCREENSHOT"
        }, timeout=20.0)
        if not res.get("ok"):
            raise RuntimeError(f"SCREENSHOT failed: {res}")

        sd_path = res.get("path")
        if not sd_path:
            raise RuntimeError(f"No screenshot path returned: {res}")

        total, wrote = download_file(ser, sd_path, out_path)
        print(f"saved_sd={sd_path}")
        print(f"saved_local={out_path}")
        print(f"bytes_expected={total} bytes_written={wrote}")


if __name__ == "__main__":
    main()
