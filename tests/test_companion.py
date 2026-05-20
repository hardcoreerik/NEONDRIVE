"""Tests for GET /api/companion/sync — Android companion heartbeat endpoint."""
import pytest
from conftest import get_json


def test_companion_sync_ok(session, base_url):
    data, r = get_json(session, base_url, "/api/companion/sync")
    assert r.status_code == 200
    assert data["ok"] is True


def test_companion_sync_shape(session, base_url):
    data, _ = get_json(session, base_url, "/api/companion/sync")
    assert "active" in data
    assert "ttl_ms" in data
    assert isinstance(data["active"], bool)
    assert isinstance(data["ttl_ms"], int)


def test_companion_sync_activates(session, base_url):
    """Two consecutive calls: first activates, second should report active=True."""
    get_json(session, base_url, "/api/companion/sync")
    data, _ = get_json(session, base_url, "/api/companion/sync")
    assert data["active"] is True


def test_companion_sync_ttl_positive_when_active(session, base_url):
    get_json(session, base_url, "/api/companion/sync")
    data, _ = get_json(session, base_url, "/api/companion/sync")
    if data["active"]:
        assert data["ttl_ms"] > 0
