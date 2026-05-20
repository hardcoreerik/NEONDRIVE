"""
Tests for the config API endpoints:
  GET/POST /api/config/keys
  GET/POST /api/config/wpasec
"""
import pytest
from conftest import get_json, post_form


# ── /api/config/keys ──────────────────────────────────────────────────────────

def test_keys_get_ok(session, base_url):
    data, r = get_json(session, base_url, "/api/config/keys")
    assert r.status_code == 200


def test_keys_get_shape(session, base_url):
    data, _ = get_json(session, base_url, "/api/config/keys")
    bool_flags = {
        "wigle_apiname_set", "wigle_apitoken_set",
        "dropbox_token_set", "webhook_url_set",
        "ntfy_topic_set", "mqtt_broker_set",
    }
    for flag in bool_flags:
        assert flag in data, f"Missing key: {flag}"
        assert isinstance(data[flag], bool), f"{flag} should be bool"

    assert "dropbox_folder" in data
    assert isinstance(data["dropbox_folder"], str)
    assert "mqtt_port" in data
    assert isinstance(data["mqtt_port"], int)
    assert 1 <= data["mqtt_port"] <= 65535
    assert "mqtt_topic_prefix" in data
    assert isinstance(data["mqtt_topic_prefix"], str)


def test_keys_post_noop_returns_ok(session, base_url):
    """POST with no recognized fields should still return ok=True."""
    data, r = post_form(session, base_url, "/api/config/keys", data={})
    assert r.status_code == 200
    assert data["ok"] is True
    assert "message" in data


def test_keys_post_dropbox_folder(session, base_url):
    """Update dropbox_folder and verify it round-trips."""
    original, _ = get_json(session, base_url, "/api/config/keys")
    original_folder = original["dropbox_folder"]

    test_folder = "/WardriveAnalyzerSync"
    post_form(session, base_url, "/api/config/keys", data={"dropbox_folder": test_folder})

    updated, _ = get_json(session, base_url, "/api/config/keys")
    assert updated["dropbox_folder"] == test_folder

    # Restore
    post_form(session, base_url, "/api/config/keys", data={"dropbox_folder": original_folder})


def test_keys_post_mqtt_port(session, base_url):
    """Update mqtt_port to a valid value and verify it round-trips."""
    original, _ = get_json(session, base_url, "/api/config/keys")
    original_port = original["mqtt_port"]

    post_form(session, base_url, "/api/config/keys", data={"mqtt_port": "1884"})
    updated, _ = get_json(session, base_url, "/api/config/keys")
    assert updated["mqtt_port"] == 1884

    # Restore
    post_form(session, base_url, "/api/config/keys", data={"mqtt_port": str(original_port)})


def test_keys_post_message_field(session, base_url):
    data, _ = post_form(session, base_url, "/api/config/keys", data={"mqtt_prefix": "nd"})
    assert "message" in data
    assert isinstance(data["message"], str)
    # Restore
    post_form(session, base_url, "/api/config/keys", data={"mqtt_prefix": "neondrive"})


# ── /api/config/wpasec ────────────────────────────────────────────────────────

def test_wpasec_get_ok(session, base_url):
    data, r = get_json(session, base_url, "/api/config/wpasec")
    assert r.status_code == 200


def test_wpasec_get_shape(session, base_url):
    data, _ = get_json(session, base_url, "/api/config/wpasec")
    assert "apikey_set" in data
    assert isinstance(data["apikey_set"], bool)


def test_wpasec_post_noop(session, base_url):
    """Firmware requires 'apikey' param; empty POST returns 400 — not a crash."""
    r = session.post(f"{base_url}/api/config/wpasec", data={}, timeout=10)
    assert r.status_code in (200, 400), f"Unexpected status: {r.status_code}"
