"""
SP3CTER — Retransmission dedup ring buffer tests.

Mirrors C++ specterIsDuplicate() + specterDedup ring in src/main.cpp.

The dedup ring is keyed on (Addr2 MAC, replay_counter) pairs.  EAPOL
retransmissions always use the same replay counter, so duplicates are detected
even across multiple retransmit cycles.

Ring capacity: SPECTER_DEDUP_MAX = 16 entries (configurable in tests via max_size).
"""
import pytest
from specter_logic import DedupRing


# ─── fixtures ────────────────────────────────────────────────────────────────

MAC_A = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01])
MAC_B = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02])
MAC_C = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03])


# ─── basic dedup behaviour ────────────────────────────────────────────────────

class TestDedupBasic:

    def test_empty_ring_is_not_duplicate(self):
        ring = DedupRing(size=16)
        assert not ring.is_duplicate(MAC_A, 0)

    def test_add_then_check(self):
        ring = DedupRing(size=16)
        ring.add(MAC_A, 1)
        assert ring.is_duplicate(MAC_A, 1)

    def test_different_mac_same_counter(self):
        """Same replay counter, different MAC → not a duplicate."""
        ring = DedupRing(size=16)
        ring.add(MAC_A, 1)
        assert not ring.is_duplicate(MAC_B, 1)

    def test_same_mac_different_counter(self):
        """Same MAC, different replay counter → not a duplicate."""
        ring = DedupRing(size=16)
        ring.add(MAC_A, 1)
        assert not ring.is_duplicate(MAC_A, 2)

    def test_multiple_entries(self):
        ring = DedupRing(size=16)
        ring.add(MAC_A, 1)
        ring.add(MAC_B, 2)
        assert ring.is_duplicate(MAC_A, 1)
        assert ring.is_duplicate(MAC_B, 2)
        assert not ring.is_duplicate(MAC_A, 2)
        assert not ring.is_duplicate(MAC_B, 1)

    def test_counter_zero(self):
        """Replay counter 0 is valid (initial handshake)."""
        ring = DedupRing(size=16)
        ring.add(MAC_A, 0)
        assert ring.is_duplicate(MAC_A, 0)

    def test_large_replay_counter(self):
        """64-bit replay counter — test at u64 boundary."""
        ring = DedupRing(size=16)
        big = (1 << 63) - 1
        ring.add(MAC_A, big)
        assert ring.is_duplicate(MAC_A, big)
        assert not ring.is_duplicate(MAC_A, big + 1)


# ─── ring wrap-around ─────────────────────────────────────────────────────────

class TestDedupRingWrap:

    def test_ring_wraps_oldest_evicted(self):
        """After filling the ring, the oldest entry is overwritten."""
        ring = DedupRing(size=4)
        # Fill ring: counters 0..3
        for i in range(4):
            ring.add(MAC_A, i)
        # All 4 should be present
        for i in range(4):
            assert ring.is_duplicate(MAC_A, i)
        # Add one more → counter 0 is evicted (head wraps)
        ring.add(MAC_A, 4)
        assert ring.is_duplicate(MAC_A, 4)
        assert not ring.is_duplicate(MAC_A, 0)

    def test_ring_retains_recent_entries_after_wrap(self):
        """After wrap, the N-1 most-recent entries are still present."""
        ring = DedupRing(size=4)
        for i in range(6):   # overflow by 2
            ring.add(MAC_A, i)
        # Most recent 4: counters 2, 3, 4, 5
        for i in range(2, 6):
            assert ring.is_duplicate(MAC_A, i)
        # Oldest two: counters 0, 1 evicted
        assert not ring.is_duplicate(MAC_A, 0)
        assert not ring.is_duplicate(MAC_A, 1)

    def test_multiple_macs_in_ring(self):
        """Ring handles interleaved (MAC, counter) pairs correctly after wrap."""
        ring = DedupRing(size=4)
        ring.add(MAC_A, 10)   # slot 0 — will be evicted by 5th add
        ring.add(MAC_B, 10)   # slot 1
        ring.add(MAC_C, 20)   # slot 2
        ring.add(MAC_A, 11)   # slot 3  (ring full, head wraps to 0)
        ring.add(MAC_C, 21)   # slot 0  (evicts MAC_A, 10)
        # (MAC_A, 10) evicted
        assert not ring.is_duplicate(MAC_A, 10)
        # remaining entries still present
        assert ring.is_duplicate(MAC_B, 10)
        assert ring.is_duplicate(MAC_C, 20)
        assert ring.is_duplicate(MAC_A, 11)
        assert ring.is_duplicate(MAC_C, 21)


# ─── EAPOL retransmission scenario ───────────────────────────────────────────

class TestEapolRetransmitScenario:

    def test_m1_retransmit(self):
        """
        AP retransmits M1 with the same replay counter if STA doesn't respond.
        The second M1 must be detected as duplicate.
        """
        ring = DedupRing(size=16)
        replay = 0x0000000000000001

        ring.add(MAC_A, replay)
        # AP retransmits — same MAC (AP BSSID), same replay counter
        assert ring.is_duplicate(MAC_A, replay)

    def test_m1_then_m3_different_counters(self):
        """M3 always has replay_counter > M1; must NOT be a duplicate of M1."""
        ring = DedupRing(size=16)
        m1_replay = 1
        m3_replay = 2  # AP increments counter

        ring.add(MAC_A, m1_replay)
        assert not ring.is_duplicate(MAC_A, m3_replay)

    def test_two_different_stas_same_counter(self):
        """Two STAs can have replay counter 1 simultaneously — not duplicates."""
        ring = DedupRing(size=16)
        ring.add(MAC_A, 1)
        ring.add(MAC_B, 1)
        assert ring.is_duplicate(MAC_A, 1)
        assert ring.is_duplicate(MAC_B, 1)

    def test_full_4way_dedup(self):
        """Simulate a 4-way HS with M1 retransmit and verify only the retx is duped."""
        ring = DedupRing(size=16)

        # M1 from AP (Addr2 = AP BSSID)
        AP  = bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66])
        STA = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF])

        ring.add(AP,  1)   # M1 original
        ring.add(STA, 1)   # M2 (STA uses same counter)
        ring.add(AP,  2)   # M3
        ring.add(STA, 2)   # M4

        # Retransmit of M1 (replay=1 from AP)
        assert ring.is_duplicate(AP, 1)
        # New M1 attempt on next session
        assert not ring.is_duplicate(AP, 3)


# ─── default capacity matches firmware ───────────────────────────────────────

class TestDedupCapacity:

    def test_default_size_16(self):
        """Default ring matches SPECTER_DEDUP_MAX = 16 in firmware."""
        ring = DedupRing()  # default size=16
        # Fill exactly 16 entries
        for i in range(16):
            ring.add(MAC_A, i)
        for i in range(16):
            assert ring.is_duplicate(MAC_A, i)
        # 17th entry evicts index 0
        ring.add(MAC_A, 16)
        assert not ring.is_duplicate(MAC_A, 0)
        assert ring.is_duplicate(MAC_A, 16)
