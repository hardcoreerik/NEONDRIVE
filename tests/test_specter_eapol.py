"""
SP3CTER — EAPOL M1/M2/M3/M4 classification tests.

Tests the Python reference implementation in specter_logic.py which mirrors
the C++ specterClassifyEapol() function in src/main.cpp.

key_info bits (big-endian u16):
  bit 3 = Install
  bit 4 = Key Ack
  bit 5 = Key MIC
  bit 6 = Secure
"""
import pytest
from specter_logic import EapolMsg, classify_eapol

# ─── helpers ────────────────────────────────────────────────────────────────

def _ki(ack=False, mic=False, secure=False, install=False) -> int:
    """
    Build a key_info word from named flags.

    Bit positions mirror the C++ specterClassifyEapol() masks:
      bit 6 (0x0040): Install
      bit 7 (0x0080): Key Ack
      bit 8 (0x0100): Key MIC
      bit 9 (0x0200): Secure
    """
    return (
        (0x0080 if ack     else 0) |
        (0x0100 if mic     else 0) |
        (0x0200 if secure  else 0) |
        (0x0040 if install else 0)
    )


def _msg(key_info: int) -> EapolMsg:
    return EapolMsg(key_info=key_info, replay_counter=1, key_data=b"")


# ─── classification correctness ─────────────────────────────────────────────

class TestM1Classification:
    """M1: Ack=1, MIC=0 (AP→STA, nonce, may contain PMKID KDE)."""

    def test_m1_pure(self):
        assert classify_eapol(_msg(_ki(ack=True, mic=False))) == "M1"

    def test_m1_with_version_bits(self):
        # Lower 3 bits are Key Descriptor Version — should not affect classification
        ki = _ki(ack=True, mic=False) | 0x02  # AES version bits
        assert classify_eapol(_msg(ki)) == "M1"

    def test_m1_key_type_pairwise(self):
        # bit 2 = Key Type (pairwise) — M1 is always pairwise
        ki = _ki(ack=True, mic=False) | (1 << 2)
        assert classify_eapol(_msg(ki)) == "M1"


class TestM2Classification:
    """M2: Ack=0, MIC=1, Secure=0 (STA→AP, STA nonce, no PMKID)."""

    def test_m2_pure(self):
        assert classify_eapol(_msg(_ki(ack=False, mic=True, secure=False))) == "M2"

    def test_m2_with_key_type(self):
        ki = _ki(ack=False, mic=True, secure=False) | (1 << 2)
        assert classify_eapol(_msg(ki)) == "M2"


class TestM3Classification:
    """M3: Ack=1, MIC=1, Install=1 (AP→STA, GTK wrapped, Install flag)."""

    def test_m3_pure(self):
        assert classify_eapol(_msg(_ki(ack=True, mic=True, install=True))) == "M3"

    def test_m3_with_secure(self):
        # M3 often also has Secure bit set — Install dominates
        ki = _ki(ack=True, mic=True, install=True, secure=True)
        assert classify_eapol(_msg(ki)) == "M3"

    def test_m3_requires_install(self):
        # Ack+MIC without Install could be ambiguous — not M3
        ki = _ki(ack=True, mic=True, install=False)
        result = classify_eapol(_msg(ki))
        assert result != "M3"


class TestM4Classification:
    """M4: Ack=0, MIC=1, Secure=1 (STA→AP, confirms receipt of GTK)."""

    def test_m4_pure(self):
        assert classify_eapol(_msg(_ki(ack=False, mic=True, secure=True))) == "M4"

    def test_m4_with_version_bits(self):
        ki = _ki(ack=False, mic=True, secure=True) | 0x02
        assert classify_eapol(_msg(ki)) == "M4"


class TestUnknownClassification:
    """Edge cases that should not match any known message."""

    def test_all_zero(self):
        assert classify_eapol(_msg(0)) == "UNKNOWN"

    def test_only_secure(self):
        # Secure=1 alone is not a valid message
        ki = _ki(secure=True)
        assert classify_eapol(_msg(ki)) == "UNKNOWN"

    def test_ack_and_secure_no_mic_is_m1(self):
        # C++ rule: Ack=1, MIC=0 → M1 regardless of Secure bit.
        # An M1 frame with Secure bit set is unusual but still classified M1.
        ki = _ki(ack=True, secure=True, mic=False)
        assert classify_eapol(_msg(ki)) == "M1"


# ─── real-world key_info values ─────────────────────────────────────────────

class TestRealWorldKeyInfo:
    """
    Observed key_info values from live WPA2 handshakes (captured in lab).
    Values sourced from Wireshark capture analysis.
    """

    # M1: 0x008A = Ack + key_type(pairwise) + version(AES)
    def test_real_m1(self):
        assert classify_eapol(_msg(0x008A)) == "M1"

    # M2: 0x010A = MIC + key_type + version
    def test_real_m2(self):
        assert classify_eapol(_msg(0x010A)) == "M2"

    # M3: 0x01CA = Ack(0x0080) + MIC(0x0100) + Install(0x0040) + KeyType(0x0008) + KDV=2(0x0002)
    # Also seen with Encrypted Key Data (bit 12 = 0x1000) → 0x13CA
    def test_real_m3(self):
        assert classify_eapol(_msg(0x01CA)) == "M3"

    def test_real_m3_with_encrypted_key_data(self):
        assert classify_eapol(_msg(0x13CA)) == "M3"

    # M4: 0x030A = MIC + Secure + key_type + version
    def test_real_m4(self):
        assert classify_eapol(_msg(0x030A)) == "M4"


# ─── EAPOL frame parsing ────────────────────────────────────────────────────

class TestEapolParsing:
    """Test find_eapol_offset and parse_eapol on synthetic frames."""

    from specter_logic import find_eapol_offset, parse_eapol

    def _make_frame(self, key_info: int, replay: int = 0, key_data: bytes = b"") -> bytes:
        """Craft a minimal frame with EAPOL Key body."""
        from specter_logic import find_eapol_offset, parse_eapol
        # 802.11 data header (24 bytes, simplified)
        dot11 = bytes(24)
        # LLC/SNAP (8 bytes): 0xAA 0xAA 0x03 0x00 0x00 0x00 0x88 0x8E
        llc = bytes([0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E])
        # EAPOL header: version(1) type(1) length(2)
        # After EtherType: version=1, type=3(Key), length=TBD
        body_after_et = self._eapol_key_body(key_info, replay, key_data)
        et = bytes([0x88, 0x8E])
        return dot11 + et + body_after_et

    def _eapol_key_body(self, key_info: int, replay: int, key_data: bytes) -> bytes:
        import struct
        version = 1
        pkt_type = 3  # Key
        descriptor = 2  # AES
        key_len = 0
        nonce = bytes(32)
        iv = bytes(16)
        rsc = bytes(8)
        id_ = bytes(8)
        mic = bytes(16)
        kdlen = len(key_data)
        payload = (
            bytes([descriptor]) +
            struct.pack(">H", key_info) +
            struct.pack(">H", key_len) +
            struct.pack(">Q", replay) +
            nonce + iv + rsc + id_ + mic +
            struct.pack(">H", kdlen) +
            key_data
        )
        length = len(payload)
        header = struct.pack(">BBH", version, pkt_type, length)
        return header + payload

    def test_find_eapol_offset(self):
        from specter_logic import find_eapol_offset
        frame = bytes(20) + bytes([0x88, 0x8E]) + bytes(10)
        assert find_eapol_offset(frame) == 20

    def test_find_eapol_offset_missing(self):
        from specter_logic import find_eapol_offset
        assert find_eapol_offset(bytes(30)) == -1

    def test_parse_m1(self):
        from specter_logic import parse_eapol
        ki = _ki(ack=True, mic=False)
        frame = self._make_frame(ki, replay=7)
        msg = parse_eapol(frame)
        assert msg is not None
        assert classify_eapol(msg) == "M1"
        assert msg.replay_counter == 7

    def test_parse_m4(self):
        from specter_logic import parse_eapol
        ki = _ki(ack=False, mic=True, secure=True)
        frame = self._make_frame(ki, replay=42)
        msg = parse_eapol(frame)
        assert msg is not None
        assert classify_eapol(msg) == "M4"
        assert msg.replay_counter == 42
