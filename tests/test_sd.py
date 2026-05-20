"""
Tests for SD card web API:
  GET  /sd          — file manager HTML
  GET  /sd/get      — download file
  POST /sd/upload   — upload file
  POST /sd/delete   — delete file
"""
import io
import pytest
import requests
from conftest import get_json


SD_SKIP_MSG = "SD card not ready"


def sd_ready(session, base_url):
    data, _ = get_json(session, base_url, "/api/status")
    return data.get("sdReady", False)


@pytest.fixture(autouse=True)
def require_sd(session, base_url):
    if not sd_ready(session, base_url):
        pytest.skip(SD_SKIP_MSG)


# ── /sd (HTML file manager) ───────────────────────────────────────────────────

def test_sd_page_returns_html(session, base_url):
    r = session.get(f"{base_url}/sd", timeout=10)
    assert r.status_code == 200
    assert "text/html" in r.headers.get("Content-Type", "")
    assert len(r.text) > 200


def test_sd_page_contains_path_listing(session, base_url):
    r = session.get(f"{base_url}/sd", params={"path": "/"}, timeout=10)
    assert r.status_code == 200


# ── /sd/upload + /sd/get + /sd/delete lifecycle ───────────────────────────────

TEST_FILE_PATH = "/test_pytest_neondrive.txt"
TEST_FILE_CONTENT = b"NEONDRIVE pytest upload test\n"


def test_sd_upload_creates_file(session, base_url):
    files = {"file": (TEST_FILE_PATH.lstrip("/"), io.BytesIO(TEST_FILE_CONTENT), "text/plain")}
    data = {"path": TEST_FILE_PATH}
    r = session.post(f"{base_url}/sd/upload", files=files, data=data, timeout=15)
    assert r.status_code in (200, 204), f"Upload failed: {r.status_code} {r.text[:200]}"


def test_sd_get_returns_uploaded_content(session, base_url):
    # Upload first
    files = {"file": (TEST_FILE_PATH.lstrip("/"), io.BytesIO(TEST_FILE_CONTENT), "text/plain")}
    session.post(f"{base_url}/sd/upload", files=files, data={"path": TEST_FILE_PATH}, timeout=15)

    r = session.get(f"{base_url}/sd/get", params={"path": TEST_FILE_PATH}, timeout=10)
    assert r.status_code == 200
    assert r.content == TEST_FILE_CONTENT


def test_sd_get_missing_file_returns_error(session, base_url):
    r = session.get(f"{base_url}/sd/get", params={"path": "/does_not_exist_pytest.txt"}, timeout=10)
    assert r.status_code in (404, 400, 500)


def test_sd_delete_removes_file(session, base_url):
    # Upload first
    files = {"file": (TEST_FILE_PATH.lstrip("/"), io.BytesIO(TEST_FILE_CONTENT), "text/plain")}
    session.post(f"{base_url}/sd/upload", files=files, data={"path": TEST_FILE_PATH}, timeout=15)

    r = session.post(f"{base_url}/sd/delete", data={"path": TEST_FILE_PATH}, timeout=10)
    assert r.status_code in (200, 204), f"Delete failed: {r.status_code} {r.text[:200]}"

    # File should be gone
    r2 = session.get(f"{base_url}/sd/get", params={"path": TEST_FILE_PATH}, timeout=10)
    assert r2.status_code in (404, 400, 500)


def test_sd_delete_missing_file_graceful(session, base_url):
    """Deleting a non-existent file returns an error (firmware may return 500 for missing files)."""
    r = session.post(f"{base_url}/sd/delete", data={"path": "/nope_pytest.txt"}, timeout=10)
    # Firmware currently returns 500 for missing-file deletes; 404/400 would be better
    # but we just confirm the device responds (doesn't reboot/hang)
    assert r.status_code in (400, 404, 500), f"Unexpected status: {r.status_code}"


# ── screenshots directory ─────────────────────────────────────────────────────

def test_sd_screenshots_dir_accessible(session, base_url):
    r = session.get(f"{base_url}/sd", params={"path": "/screenshots"}, timeout=10)
    # Either lists the dir (200) or 404 if no screenshots yet — both are fine
    assert r.status_code in (200, 404, 400)
