"""
SP3CTER — Handshake completeness tracking tests.

Mirrors C++ specterFindOrCreateHsTrack() + SpecterHsTrack struct in src/main.cpp.

The tracker holds up to SPECTER_HS_MAX (6) entries; when full it evicts the
entry with the oldest lastSeenMs timestamp (LRU eviction).
"""
import pytest
from specter_logic import HsTrack, HsTracker


# ─── fixtures ────────────────────────────────────────────────────────────────

STA1 = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01])
STA2 = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02])
STA3 = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03])
STA4 = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x04])
STA5 = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x05])
STA6 = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x06])
STA7 = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x07])


# ─── HsTrack unit tests ───────────────────────────────────────────────────────

class TestHsTrack:

    def test_initial_state(self):
        t = HsTrack(sta_mac=STA1)
        assert not t.m1
        assert not t.m2
        assert not t.m3
        assert not t.m4
        assert not t.is_complete
        assert t.completeness == 0

    def test_update_m1(self):
        t = HsTrack(sta_mac=STA1)
        t.update("M1")
        assert t.m1
        assert not t.m2
        assert t.completeness == 1

    def test_update_all_four(self):
        t = HsTrack(sta_mac=STA1)
        for msg in ("M1", "M2", "M3", "M4"):
            t.update(msg)
        assert t.is_complete
        assert t.completeness == 4

    def test_idempotent_update(self):
        """Updating the same message twice should not double-count."""
        t = HsTrack(sta_mac=STA1)
        t.update("M1")
        t.update("M1")
        assert t.completeness == 1

    def test_update_unknown_ignored(self):
        t = HsTrack(sta_mac=STA1)
        t.update("UNKNOWN")
        assert t.completeness == 0

    def test_partial_three_of_four(self):
        t = HsTrack(sta_mac=STA1)
        for msg in ("M1", "M2", "M3"):
            t.update(msg)
        assert not t.is_complete
        assert t.completeness == 3

    def test_m2_m4_without_m1(self):
        """M2+M4 seen (M1+M3 missed) — still valid partial capture."""
        t = HsTrack(sta_mac=STA1)
        t.update("M2")
        t.update("M4")
        assert t.completeness == 2
        assert not t.is_complete


# ─── HsTracker table management ──────────────────────────────────────────────

class TestHsTracker:

    def test_empty_get(self):
        tracker = HsTracker(max_size=6)
        assert tracker.get(STA1) is None

    def test_record_creates_entry(self):
        tracker = HsTracker(max_size=6)
        tracker.record(STA1, "M1")
        entry = tracker.get(STA1)
        assert entry is not None
        assert entry.m1 is True

    def test_record_same_sta_accumulates(self):
        tracker = HsTracker(max_size=6)
        tracker.record(STA1, "M1")
        tracker.record(STA1, "M2")
        tracker.record(STA1, "M3")
        tracker.record(STA1, "M4")
        entry = tracker.get(STA1)
        assert entry.is_complete

    def test_multiple_stas(self):
        tracker = HsTracker(max_size=6)
        tracker.record(STA1, "M1")
        tracker.record(STA2, "M2")
        assert tracker.get(STA1).m1 is True
        assert tracker.get(STA2).m2 is True
        assert not tracker.get(STA1).m2  # STA1's M2 not set

    def test_count_grows(self):
        tracker = HsTracker(max_size=6)
        for i, sta in enumerate([STA1, STA2, STA3]):
            tracker.record(sta, "M1")
            assert tracker.count == i + 1

    def test_lru_eviction_at_max(self):
        """When table is full (6 entries), the oldest is evicted."""
        tracker = HsTracker(max_size=6)
        # STA1 recorded first (will be LRU)
        tracker.record(STA1, "M1")
        for sta in [STA2, STA3, STA4, STA5, STA6]:
            tracker.record(sta, "M1")
        # Now at capacity (6). Adding STA7 should evict STA1.
        tracker.record(STA7, "M1")
        assert tracker.count == 6
        assert tracker.get(STA1) is None   # evicted
        assert tracker.get(STA7) is not None

    def test_lru_eviction_keeps_recently_touched(self):
        """Re-touching an entry promotes it; the truly-oldest is evicted."""
        tracker = HsTracker(max_size=6)
        for sta in [STA1, STA2, STA3, STA4, STA5, STA6]:
            tracker.record(sta, "M1")
        # Re-touch STA1 so it's no longer LRU; STA2 becomes oldest
        tracker.record(STA1, "M2")
        tracker.record(STA7, "M1")  # should evict STA2
        assert tracker.get(STA1) is not None   # re-touched, kept
        assert tracker.get(STA2) is None       # evicted

    def test_complete_handshake_in_table(self):
        tracker = HsTracker(max_size=6)
        for msg in ("M1", "M2", "M3", "M4"):
            tracker.record(STA1, msg)
        assert tracker.get(STA1).is_complete

    def test_return_value_is_live(self):
        """record() return value reflects current state after update."""
        tracker = HsTracker(max_size=6)
        t = tracker.record(STA1, "M1")
        assert t.m1 is True
        t2 = tracker.record(STA1, "M3")
        assert t2.m3 is True


# ─── handshake sequence integrity ────────────────────────────────────────────

class TestHandshakeSequence:

    @pytest.mark.parametrize("sequence,expected_completeness", [
        (["M1"],              1),
        (["M1", "M2"],        2),
        (["M1", "M2", "M3"], 3),
        (["M1", "M2", "M3", "M4"], 4),
        (["M2", "M4"],        2),   # Partial capture (M1+M3 missed)
        (["M1", "M1", "M2"], 2),   # Retransmission of M1
    ])
    def test_completeness(self, sequence, expected_completeness):
        tracker = HsTracker(max_size=6)
        for msg in sequence:
            tracker.record(STA1, msg)
        assert tracker.get(STA1).completeness == expected_completeness

    def test_out_of_order_delivery(self):
        """Frames can arrive out-of-order; all four should still be recorded."""
        tracker = HsTracker(max_size=6)
        for msg in ["M3", "M1", "M4", "M2"]:
            tracker.record(STA1, msg)
        assert tracker.get(STA1).is_complete
