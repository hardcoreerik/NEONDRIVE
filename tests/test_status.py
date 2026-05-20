"""Tests for GET /api/status — device health and operational state."""
import pytest
from conftest import get_json


REQUIRED_KEYS = {
    "ok", "name", "mode", "autoMode", "sniffActive", "lockChannel",
    "apMode", "webServer", "sdReady", "packets", "beacons", "handshakes",
    "ip_sta", "ip_ap",
}


def test_status_ok(session, base_url):
    data, r = get_json(session, base_url, "/api/status")
    assert r.status_code == 200
    assert data["ok"] is True


def test_status_content_type(session, base_url):
    _, r = get_json(session, base_url, "/api/status")
    assert "application/json" in r.headers.get("Content-Type", "")


def test_status_required_fields(session, base_url):
    data, _ = get_json(session, base_url, "/api/status")
    missing = REQUIRED_KEYS - set(data.keys())
    assert not missing, f"Missing fields: {missing}"


def test_status_field_types(session, base_url):
    data, _ = get_json(session, base_url, "/api/status")
    assert isinstance(data["mode"], int)
    assert isinstance(data["autoMode"], str)
    assert isinstance(data["sniffActive"], bool)
    assert isinstance(data["lockChannel"], bool)
    assert isinstance(data["apMode"], bool)
    assert isinstance(data["webServer"], bool)
    assert isinstance(data["sdReady"], bool)
    assert isinstance(data["packets"], int)
    assert isinstance(data["beacons"], int)
    assert isinstance(data["handshakes"], int)
    assert data["packets"] >= 0
    assert data["beacons"] >= 0
    assert data["handshakes"] >= 0


def test_status_name(session, base_url):
    data, _ = get_json(session, base_url, "/api/status")
    assert isinstance(data["name"], str)
    assert len(data["name"]) > 0


def test_status_ip_format(session, base_url):
    data, _ = get_json(session, base_url, "/api/status")
    for field in ("ip_sta", "ip_ap"):
        ip = data[field]
        assert isinstance(ip, str)
        # Valid IPv4 or "0.0.0.0" (disconnected)
        parts = ip.split(".")
        assert len(parts) == 4, f"{field}={ip!r} is not a valid IPv4"
        assert all(p.isdigit() and 0 <= int(p) <= 255 for p in parts)


def test_status_webserver_true(session, base_url):
    """The webserver must be running for these tests to work at all."""
    data, _ = get_json(session, base_url, "/api/status")
    assert data["webServer"] is True, "webServer field should be True (we're connected)"


def test_status_yoink_absent_when_idle(session, base_url):
    """yoink key should not appear unless YOINK auto-mode is active."""
    data, _ = get_json(session, base_url, "/api/status")
    if data["autoMode"] != "Y0INK":
        assert "yoink" not in data, "yoink key present but autoMode is not Y0INK"


def test_status_yoink_present_when_active(session, base_url):
    """If YOINK is running, the yoink sub-object must have state and hs."""
    data, _ = get_json(session, base_url, "/api/status")
    if data["autoMode"] == "Y0INK" and "yoink" in data:
        yoink = data["yoink"]
        assert "state" in yoink
        assert "hs" in yoink
        assert isinstance(yoink["hs"], int)
        assert yoink["hs"] >= 0
