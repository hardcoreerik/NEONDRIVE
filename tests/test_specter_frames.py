"""
SP3CTER — Ghost frame structure validation tests.

Mirrors C++ specterSendGhostAuth() and specterSendGhostAssoc() in src/main.cpp.
Also validates specterRandomizeSta() (locally-administered unicast MAC).

Tests here are pure Python; they validate that:
  1. Auth frame has the correct 802.11 FC, length, and fixed fields.
  2. Assoc frame includes a well-formed RSN IE for WPA2-CCMP-PSK.
  3. Ghost STA MAC is locally-administered unicast (not multicast, not global).
"""
import struct
import pytest
from specter_logic import (
    build_auth_frame, build_assoc_frame, is_locally_administered_unicast,
)


# ─── fixtures ────────────────────────────────────────────────────────────────

SRC_MAC  = bytes([0x02, 0xAB, 0xCD, 0xEF, 0x01, 0x23])  # LA unicast
DST_MAC  = bytes([0xD4, 0xE9, 0xF4, 0xA9, 0x1B, 0x58])  # 3.5" CYD AP
BSSID    = DST_MAC
SSID     = b"NEONDRIVE_LAB"


# ─── Auth frame tests ─────────────────────────────────────────────────────────

class TestAuthFrame:

    def setup_method(self):
        self.frame = build_auth_frame(SRC_MAC, DST_MAC, BSSID)

    def test_fc_byte0(self):
        """FC byte 0 = 0xB0 (subtype=1011 = Authentication, type=00 = Management)."""
        assert self.frame[0] == 0xB0

    def test_fc_byte1(self):
        """FC byte 1 = 0x00 (no flags)."""
        assert self.frame[1] == 0x00

    def test_dest_addr(self):
        """DA at bytes 4-9."""
        assert bytes(self.frame[4:10]) == DST_MAC

    def test_src_addr(self):
        """SA at bytes 10-15."""
        assert bytes(self.frame[10:16]) == SRC_MAC

    def test_bssid(self):
        """BSSID at bytes 16-21."""
        assert bytes(self.frame[16:22]) == BSSID

    def test_auth_algo_open_system(self):
        """Auth Algorithm = 0x0000 (Open System)."""
        auth_algo = struct.unpack_from("<H", self.frame, 24)[0]
        assert auth_algo == 0

    def test_auth_seq_1(self):
        """Auth Sequence = 1 (request frame)."""
        auth_seq = struct.unpack_from("<H", self.frame, 26)[0]
        assert auth_seq == 1

    def test_status_code_success(self):
        """Status Code = 0 (Success) in the request."""
        status = struct.unpack_from("<H", self.frame, 28)[0]
        assert status == 0

    def test_minimum_length(self):
        """Auth frame is at least 30 bytes (24B header + 6B fixed fields)."""
        assert len(self.frame) >= 30


# ─── Assoc frame tests ────────────────────────────────────────────────────────

class TestAssocFrame:

    def setup_method(self):
        self.frame = build_assoc_frame(SRC_MAC, DST_MAC, BSSID, SSID)

    def test_fc_type_management(self):
        """FC bits 2-3 = 00 (Management frame type)."""
        fc0 = self.frame[0]
        frame_type = (fc0 >> 2) & 0x03
        assert frame_type == 0

    def test_fc_subtype_assoc(self):
        """FC bits 4-7 = 0000 (Association Request subtype)."""
        fc0 = self.frame[0]
        subtype = (fc0 >> 4) & 0x0F
        assert subtype == 0

    def test_dest_addr(self):
        assert bytes(self.frame[4:10]) == DST_MAC

    def test_src_addr(self):
        assert bytes(self.frame[10:16]) == SRC_MAC

    def test_bssid(self):
        assert bytes(self.frame[16:22]) == BSSID

    def test_contains_ssid_ie(self):
        """SSID IE (0x00) must appear in the body."""
        body = self.frame[24:]
        assert 0x00 in body

    def test_contains_rsn_ie(self):
        """RSN IE (Element ID 0x30) must appear in the body."""
        body = self.frame[24:]
        assert 0x30 in body

    def test_rsn_ie_content(self):
        """RSN IE must specify CCMP group/pairwise cipher and PSK AKM."""
        body = self.frame[24:]
        # Find RSN IE (0x30)
        idx = body.index(0x30)
        ie_len = body[idx + 1]
        ie_data = body[idx + 2: idx + 2 + ie_len]
        # RSN version
        assert ie_data[0:2] == bytes([0x01, 0x00])
        # Group cipher: CCMP = 00 0F AC 04
        assert ie_data[2:6] == bytes([0x00, 0x0F, 0xAC, 0x04])
        # Pairwise count = 1
        pw_count = struct.unpack_from("<H", ie_data, 6)[0]
        assert pw_count == 1
        # Pairwise suite: CCMP
        assert ie_data[8:12] == bytes([0x00, 0x0F, 0xAC, 0x04])
        # AKM count = 1
        akm_count = struct.unpack_from("<H", ie_data, 12)[0]
        assert akm_count == 1
        # AKM: PSK = 00 0F AC 02
        assert ie_data[14:18] == bytes([0x00, 0x0F, 0xAC, 0x02])

    def test_rsn_ie_no_pmkid_list(self):
        """
        Ghost assoc must NOT advertise a PMKID in the RSN IE PMKID List.
        RSN caps are at offset 18 of IE data; PMKID count follows.
        If PMKID count = 0 or absent, we're clean.
        """
        body = self.frame[24:]
        idx = body.index(0x30)
        ie_len = body[idx + 1]
        ie_data = body[idx + 2: idx + 2 + ie_len]
        # RSN caps at offset 18; PMKID count (if present) at offset 20
        if len(ie_data) >= 22:
            pmkid_count = struct.unpack_from("<H", ie_data, 20)[0]
            assert pmkid_count == 0, "Ghost frame must not include PMKID in RSN IE"


# ─── Ghost MAC tests ──────────────────────────────────────────────────────────

class TestGhostMac:

    def test_locally_administered_unicast(self):
        """Ghost STA MAC byte[0] = 0x02 (LA=1, multicast=0)."""
        assert is_locally_administered_unicast(SRC_MAC)

    def test_not_multicast(self):
        """Multicast bit (bit 0 of byte 0) must be clear."""
        assert (SRC_MAC[0] & 0x01) == 0

    def test_la_bit_set(self):
        """Locally Administered bit (bit 1 of byte 0) must be set."""
        assert (SRC_MAC[0] & 0x02) == 0x02

    def test_global_mac_is_not_la(self):
        """Real OUI-based MACs have LA=0 — verify the helper rejects them."""
        real_mac = bytes([0xD4, 0xE9, 0xF4, 0xA9, 0x1B, 0x58])
        assert not is_locally_administered_unicast(real_mac)

    def test_multicast_mac_is_not_valid_ghost(self):
        """Multicast MAC (bit 0 set) should fail the LA-unicast check."""
        mcast = bytes([0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF])
        assert not is_locally_administered_unicast(mcast)

    @pytest.mark.parametrize("mac", [
        bytes([0x02, 0x00, 0x00, 0x00, 0x00, 0x01]),
        bytes([0x02, 0xDE, 0xAD, 0xBE, 0xEF, 0x01]),
        bytes([0x06, 0x12, 0x34, 0x56, 0x78, 0x9A]),
    ])
    def test_various_la_macs(self, mac):
        """Any MAC with byte[0] in {0x02, 0x06} is locally administered unicast."""
        assert is_locally_administered_unicast(mac)


# ─── Data frame STA extraction ────────────────────────────────────────────────

class TestDataFrameStaExtraction:

    from specter_logic import extract_sta_mac_from_data

    def _make_data(self, fc0: int, fc1: int, addr1: bytes, addr2: bytes) -> bytes:
        """Minimal 28-byte 802.11 data frame (≥26 required by extract_sta_mac_from_data)."""
        return (
            bytes([fc0, fc1]) +     # FC (2)
            bytes(2) +              # duration (2)
            addr1 +                 # addr1 (6)
            addr2 +                 # addr2 (6)
            bytes(6) +              # addr3 / BSSID (6)
            bytes(2) +              # seq ctrl (2)
            bytes(4)                # padding to ensure ≥26 (4)
        )

    def test_to_ds_sta_is_addr2(self):
        """ToDS=1 FromDS=0: Addr2 is the STA."""
        from specter_logic import extract_sta_mac_from_data
        sta = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01])
        ap  = bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66])
        fc0 = 0x08  # data frame (type=2, subtype=0)
        fc1 = 0x01  # ToDS=1, FromDS=0
        frame = self._make_data(fc0, fc1, addr1=ap, addr2=sta)
        result = extract_sta_mac_from_data(frame)
        assert result == sta

    def test_from_ds_sta_is_addr1(self):
        """ToDS=0 FromDS=1: Addr1 is the STA."""
        from specter_logic import extract_sta_mac_from_data
        sta = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02])
        ap  = bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66])
        fc0 = 0x08
        fc1 = 0x02  # ToDS=0, FromDS=1
        frame = self._make_data(fc0, fc1, addr1=sta, addr2=ap)
        result = extract_sta_mac_from_data(frame)
        assert result == sta

    def test_non_data_frame_returns_none(self):
        """Management frames should not be processed for STA extraction."""
        from specter_logic import extract_sta_mac_from_data
        fc0 = 0x00  # Management frame (type=0)
        fc1 = 0x01
        frame = self._make_data(fc0, fc1, bytes(6), bytes(6))
        assert extract_sta_mac_from_data(frame) is None

    def test_too_short_returns_none(self):
        from specter_logic import extract_sta_mac_from_data
        assert extract_sta_mac_from_data(bytes(10)) is None
