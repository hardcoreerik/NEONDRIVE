"""Tests for WPA-SEC sync and results endpoints."""
import pytest
from conftest import get_json


def test_wpasec_results_ok_or_not_configured(session, base_url):
    """Either returns results JSON or a plain-text/HTML error if not configured."""
    r = session.get(f"{base_url}/wpasec/results", timeout=10)
    assert r.status_code in (200, 400, 401, 503)
    if r.status_code == 200 and "application/json" in r.headers.get("Content-Type", ""):
        data = r.json()
        assert "ok" in data


def test_wpasec_sync_requires_key(session, base_url):
    """Sync should fail gracefully with no API key configured."""
    data, _ = get_json(session, base_url, "/api/config/wpasec")
    if data["apikey_set"]:
        pytest.skip("API key is set; skipping no-key test")
    r = session.get(f"{base_url}/wpasec/sync", timeout=10)
    assert r.status_code in (200, 400, 401, 503)


def test_wpasec_download_requires_key(session, base_url):
    data, _ = get_json(session, base_url, "/api/config/wpasec")
    if data["apikey_set"]:
        pytest.skip("API key is set; skipping no-key test")
    r = session.get(f"{base_url}/wpasec/download", timeout=10)
    assert r.status_code in (200, 400, 401, 503)
    # Must not crash
    assert r.status_code < 500


def test_wpasec_results_download_returns_file_or_error(session, base_url):
    r = session.get(f"{base_url}/wpasec/results/download", timeout=10)
    assert r.status_code < 500
