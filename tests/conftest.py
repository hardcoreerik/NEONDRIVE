"""
NEONDRIVE pytest configuration.

Set NEONDRIVE_HOST to override the default device address:
    NEONDRIVE_HOST=192.168.1.169:8080 pytest tests/
"""
import os
import pytest
import requests


DEFAULT_HOST = "192.168.1.169:8080"


def pytest_addoption(parser):
    parser.addoption(
        "--host",
        default=os.environ.get("NEONDRIVE_HOST", DEFAULT_HOST),
        help="NEONDRIVE device host:port (default: %(default)s)",
    )


@pytest.fixture(scope="session")
def host(request):
    return request.config.getoption("--host")


@pytest.fixture(scope="session")
def base_url(host):
    return f"http://{host}"


@pytest.fixture(scope="session")
def session(base_url):
    """Requests session with a short timeout. Skips entire session if device is unreachable."""
    s = requests.Session()
    s.headers.update({"Accept": "application/json"})
    try:
        r = s.get(f"{base_url}/api/status", timeout=5)
        r.raise_for_status()
    except Exception as exc:
        pytest.skip(f"Device not reachable at {base_url}: {exc}")
    return s


def get_json(session, base_url, path, params=None, timeout=10):
    """GET and parse JSON; raises on HTTP error."""
    r = session.get(f"{base_url}{path}", params=params, timeout=timeout)
    r.raise_for_status()
    return r.json(), r


def post_form(session, base_url, path, data, timeout=10):
    """POST form-encoded data; raises on HTTP error."""
    r = session.post(f"{base_url}{path}", data=data, timeout=timeout)
    r.raise_for_status()
    return r.json(), r
