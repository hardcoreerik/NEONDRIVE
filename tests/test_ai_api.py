"""
Tests for the AI/injection API:
  GET  /api/ai/frames
  POST /api/ai/inject_deauth
  POST /api/ai/inject_raw
"""
import json
import pytest
from conftest import get_json, post_form


def test_ai_frames_ok(session, base_url):
    data, r = get_json(session, base_url, "/api/ai/frames")
    assert r.status_code == 200
    assert data["ok"] is True


def test_ai_frames_shape(session, base_url):
    data, _ = get_json(session, base_url, "/api/ai/frames")
    # Firmware uses "handshakes" as the frame list key
    frame_key = "frames" if "frames" in data else "handshakes"
    assert frame_key in data
    assert isinstance(data[frame_key], list)


def test_ai_frames_entry_fields(session, base_url):
    data, _ = get_json(session, base_url, "/api/ai/frames")
    frame_key = "frames" if "frames" in data else "handshakes"
    for frame in data[frame_key]:
        assert "ts" in frame or "bssid" in frame  # at least one known field


def test_inject_deauth_missing_body(session, base_url):
    """POST with no body should return a structured error, not crash."""
    r = session.post(f"{base_url}/api/ai/inject_deauth", timeout=10)
    assert r.status_code < 500
    if r.headers.get("Content-Type", "").startswith("application/json"):
        data = r.json()
        assert "ok" in data


def test_inject_deauth_invalid_mac(session, base_url):
    payload = json.dumps({"bssid": "ZZ:ZZ:ZZ:ZZ:ZZ:ZZ", "client": "FF:FF:FF:FF:FF:FF"})
    r = session.post(
        f"{base_url}/api/ai/inject_deauth",
        data=payload,
        headers={"Content-Type": "application/json"},
        timeout=10,
    )
    assert r.status_code < 500
    if r.headers.get("Content-Type", "").startswith("application/json"):
        data = r.json()
        # Should report error, not ok
        assert data.get("ok") is False or "error" in data


def test_inject_raw_missing_body(session, base_url):
    r = session.post(f"{base_url}/api/ai/inject_raw", timeout=10)
    assert r.status_code < 500


def test_inject_raw_invalid_hex(session, base_url):
    payload = json.dumps({"hex": "ZZZZ", "channel": 1})
    r = session.post(
        f"{base_url}/api/ai/inject_raw",
        data=payload,
        headers={"Content-Type": "application/json"},
        timeout=10,
    )
    assert r.status_code < 500
    if r.headers.get("Content-Type", "").startswith("application/json"):
        data = r.json()
        assert data.get("ok") is False or "error" in data
