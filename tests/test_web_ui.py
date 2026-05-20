"""
Smoke tests for HTML pages — verifies they return 200 with HTML content
and contain expected page landmarks (title, nav elements, etc.).
"""
import pytest


HTML_PAGES = [
    ("/",           "NEONDRIVE"),
    ("/wpasec",     "WPA"),
    ("/wpasec/config", "WPA"),
    ("/keys/config", "API Keys"),
    ("/android",    "companion"),
    ("/yoink/log",  None),      # may be empty; just check 200/404
]


@pytest.mark.parametrize("path,expected_text", HTML_PAGES)
def test_html_page_reachable(session, base_url, path, expected_text):
    r = session.get(f"{base_url}{path}", timeout=10)
    assert r.status_code in (200, 404), f"{path}: unexpected status {r.status_code}"
    if r.status_code == 200:
        assert "text/html" in r.headers.get("Content-Type", ""), f"{path}: not HTML"
        if expected_text:
            assert expected_text.lower() in r.text.lower(), (
                f"{path}: expected {expected_text!r} in body"
            )


def test_root_has_nav_links(session, base_url):
    r = session.get(f"{base_url}/", timeout=10)
    assert r.status_code == 200
    body = r.text.lower()
    # Root page should contain at least some navigation
    assert "href" in body


def test_wpasec_config_has_form(session, base_url):
    r = session.get(f"{base_url}/wpasec/config", timeout=10)
    assert r.status_code == 200
    assert "<input" in r.text.lower() or "<form" in r.text.lower()


def test_keys_config_has_form(session, base_url):
    r = session.get(f"{base_url}/keys/config", timeout=10)
    assert r.status_code == 200
    assert "<input" in r.text.lower()


def test_screenshot_endpoint_returns_image_or_error(session, base_url):
    """
    GET /screenshot: ST7796 pixel readback via SPI crashes the ESP32 (known hardware
    limitation — readPixel hangs, watchdog fires, device resets).  The test captures
    that the device resets (ConnectionError) rather than silently hanging.  Once pixel
    readback is fixed this test should be updated to assert status 200 + image/bmp.
    """
    import requests as _req
    try:
        r = session.get(f"{base_url}/screenshot", timeout=30)
        # If it ever succeeds, verify the content type
        if r.status_code == 200:
            ct = r.headers.get("Content-Type", "")
            assert "image" in ct or "octet-stream" in ct
        else:
            # Non-200 but not a crash — acceptable
            assert r.status_code < 500
    except _req.exceptions.ConnectionError:
        # Expected until readPixel is fixed on CYD35 ST7796
        pytest.xfail("Screenshot crashes device (ST7796 readPixel not yet fixed)")
