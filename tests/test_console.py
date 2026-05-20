"""Tests for GET /api/console — ring-buffer event log."""
import pytest
from conftest import get_json


def test_console_ok(session, base_url):
    data, r = get_json(session, base_url, "/api/console")
    assert r.status_code == 200
    assert data["ok"] is True


def test_console_shape(session, base_url):
    data, _ = get_json(session, base_url, "/api/console")
    assert "next_seq" in data
    assert "lines" in data
    assert isinstance(data["next_seq"], int)
    assert isinstance(data["lines"], list)
    assert data["next_seq"] >= 0


def test_console_line_fields(session, base_url):
    data, _ = get_json(session, base_url, "/api/console")
    for line in data["lines"]:
        assert "seq" in line
        assert "level" in line
        assert "tag" in line
        assert "msg" in line
        assert isinstance(line["seq"], int)
        assert isinstance(line["level"], str)
        assert isinstance(line["msg"], str)


def test_console_since_filters(session, base_url):
    """since=<next_seq> should return 0 lines (nothing newer yet)."""
    data1, _ = get_json(session, base_url, "/api/console")
    next_seq = data1["next_seq"]
    data2, _ = get_json(session, base_url, "/api/console", params={"since": next_seq})
    assert data2["ok"] is True
    assert data2["lines"] == []


def test_console_limit_respected(session, base_url):
    data, _ = get_json(session, base_url, "/api/console", params={"limit": 5})
    assert data["ok"] is True
    assert len(data["lines"]) <= 5


def test_console_limit_zero_clamps_to_one(session, base_url):
    """limit=0 should be clamped to 1 by firmware."""
    data, _ = get_json(session, base_url, "/api/console", params={"limit": 0})
    assert data["ok"] is True
    assert len(data["lines"]) <= 1


def test_console_sequences_ascending(session, base_url):
    data, _ = get_json(session, base_url, "/api/console")
    seqs = [line["seq"] for line in data["lines"]]
    assert seqs == sorted(seqs), "Console lines are not in ascending seq order"
