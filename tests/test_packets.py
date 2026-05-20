"""Tests for GET /api/packets — sniffer packet ring buffer."""
import pytest
from conftest import get_json


def test_packets_ok(session, base_url):
    data, r = get_json(session, base_url, "/api/packets")
    assert r.status_code == 200
    assert data["ok"] is True


def test_packets_shape(session, base_url):
    data, _ = get_json(session, base_url, "/api/packets")
    assert "lines" in data
    assert "next_seq" in data
    assert isinstance(data["lines"], list)
    assert isinstance(data["next_seq"], int)
    assert data["next_seq"] >= 0


def test_packets_line_fields(session, base_url):
    data, _ = get_json(session, base_url, "/api/packets")
    for pkt in data["lines"]:
        assert "seq" in pkt
        assert "ts" in pkt
        assert "type" in pkt
        assert isinstance(pkt["seq"], int)
        assert isinstance(pkt["ts"], int)
        assert isinstance(pkt["type"], str)


def test_packets_since_empty(session, base_url):
    data1, _ = get_json(session, base_url, "/api/packets")
    next_seq = data1["next_seq"]
    data2, _ = get_json(session, base_url, "/api/packets", params={"since": next_seq})
    assert data2["ok"] is True
    assert data2["lines"] == []


def test_packets_limit(session, base_url):
    data, _ = get_json(session, base_url, "/api/packets", params={"limit": 3})
    assert data["ok"] is True
    assert len(data["lines"]) <= 3


def test_packets_sequences_ascending(session, base_url):
    data, _ = get_json(session, base_url, "/api/packets")
    seqs = [p["seq"] for p in data["lines"]]
    assert seqs == sorted(seqs)
