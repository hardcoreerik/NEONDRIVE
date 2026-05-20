"""Tests for GET /api/yoink and GET /yoink/log."""
import pytest
from conftest import get_json


def test_yoink_api_ok(session, base_url):
    data, r = get_json(session, base_url, "/api/yoink")
    assert r.status_code == 200
    assert data["ok"] is True


def test_yoink_api_shape(session, base_url):
    data, _ = get_json(session, base_url, "/api/yoink")
    assert "running" in data
    assert "state" in data
    assert isinstance(data["running"], bool)
    assert isinstance(data["state"], str)


def test_yoink_api_counters(session, base_url):
    data, _ = get_json(session, base_url, "/api/yoink")
    for key in ("handshakes", "targets", "deauths"):
        if key in data:
            assert isinstance(data[key], int)
            assert data[key] >= 0


def test_yoink_log_page(session, base_url):
    r = session.get(f"{base_url}/yoink/log", timeout=10)
    # 200 (log exists) or 404/empty (no session yet) — both valid
    assert r.status_code in (200, 404)
    if r.status_code == 200:
        assert "text/html" in r.headers.get("Content-Type", "")


def test_yoink_since_parameter(session, base_url):
    """since= should be accepted without error."""
    data, r = get_json(session, base_url, "/api/yoink", params={"since": 0})
    assert r.status_code == 200
    assert data["ok"] is True
