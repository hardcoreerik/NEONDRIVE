"""
SP3CTER — PMKID KDE extraction and hc22000 format tests.

Mirrors C++ specterExtractPmkid() and specterWriteHc22000() in src/main.cpp.

PMKID KDE format (as it appears in EAPOL M1 key_data):
  DD 14 00 0F AC 04 <16 bytes PMKID>
  ^  ^  ^^^^^^^^^^^
  |  |  OUI+type (00 0F AC 04)
  |  Length (0x14 = 20 = 4 header bytes for OUI+type + 16 PMKID bytes)
  Element ID (0xDD = Vendor Specific)

hc22000 line: {pmkid_hex}*{ap_mac_hex}*{sta_mac_hex}*{ssid_hex}
All MAC hex has NO colons or separators.
"""
import pytest
from specter_logic import extract_pmkid, format_hc22000, PMKID_LEN


# ─── known test vectors ──────────────────────────────────────────────────────

KNOWN_PMKID   = bytes.fromhex("4d4fe7aac3a2cecab195321ccd5ab4bc")
KNOWN_AP_MAC  = bytes.fromhex("d4e9f4a91b58")   # 3.5" CYD lab AP
KNOWN_STA_MAC = bytes.fromhex("d4e9f4b4958c")   # 2.4" CYD lab STA
KNOWN_SSID    = b"NEONDRIVE_LAB"


def _make_kde_data(pmkid: bytes) -> bytes:
    """Wrap a PMKID in the vendor-specific KDE envelope."""
    return bytes([0xDD, 0x14, 0x00, 0x0F, 0xAC, 0x04]) + pmkid


# ─── extraction tests ────────────────────────────────────────────────────────

class TestPmkidExtraction:

    def test_extract_single(self):
        """Basic extraction from a clean KDE block."""
        kd = _make_kde_data(KNOWN_PMKID)
        result = extract_pmkid(kd)
        assert result == KNOWN_PMKID

    def test_extract_length(self):
        """Extracted PMKID is always 16 bytes."""
        kd = _make_kde_data(KNOWN_PMKID)
        result = extract_pmkid(kd)
        assert result is not None
        assert len(result) == PMKID_LEN

    def test_extract_with_preceding_kde(self):
        """PMKID KDE preceded by an RSN Info KDE (common in real M1 frames)."""
        # Prepend a dummy KDE: type=0xDD len=6 data=000000000000
        dummy_kde = bytes([0xDD, 0x06, 0x00, 0x50, 0xF2, 0x01, 0xAB, 0xCD])
        kd = dummy_kde + _make_kde_data(KNOWN_PMKID)
        result = extract_pmkid(kd)
        assert result == KNOWN_PMKID

    def test_extract_missing(self):
        """Returns None when no PMKID KDE is present."""
        kd = bytes([0x30, 0x14] + [0x00] * 20)  # RSN IE, not a PMKID KDE
        assert extract_pmkid(kd) is None

    def test_extract_empty(self):
        """Returns None for empty key_data (no PMKID in plain EAPOL frames)."""
        assert extract_pmkid(b"") is None

    def test_extract_truncated(self):
        """Returns None when KDE is cut off before full PMKID."""
        kd = bytes([0xDD, 0x14, 0x00, 0x0F, 0xAC, 0x04]) + bytes(8)  # only 8 of 16 bytes
        assert extract_pmkid(kd) is None

    def test_extract_wrong_oui(self):
        """KDE with wrong OUI is ignored."""
        kd = bytes([0xDD, 0x14, 0x00, 0x0F, 0xAC, 0x02]) + bytes(16)  # AKM PSK, not PMKID
        assert extract_pmkid(kd) is None

    def test_extract_all_zeros_pmkid(self):
        """Zero PMKID is still a valid extraction (AP just hasn't computed it)."""
        zero_pmkid = bytes(16)
        kd = _make_kde_data(zero_pmkid)
        result = extract_pmkid(kd)
        assert result == zero_pmkid

    def test_extract_multiple_kdes_picks_pmkid(self):
        """Correctly picks the PMKID KDE when it appears after several other KDEs."""
        other1 = bytes([0xDD, 0x04, 0xAA, 0xBB, 0xCC, 0xDD])
        other2 = bytes([0xDD, 0x04, 0x11, 0x22, 0x33, 0x44])
        pmkid_kde = _make_kde_data(KNOWN_PMKID)
        result = extract_pmkid(other1 + other2 + pmkid_kde)
        assert result == KNOWN_PMKID


# ─── hc22000 format tests ────────────────────────────────────────────────────

class TestHc22000Format:

    def test_format_basic(self):
        line = format_hc22000(KNOWN_PMKID, KNOWN_AP_MAC, KNOWN_STA_MAC, KNOWN_SSID)
        pmkid_hex  = KNOWN_PMKID.hex()
        ap_hex     = KNOWN_AP_MAC.hex()
        sta_hex    = KNOWN_STA_MAC.hex()
        ssid_hex   = KNOWN_SSID.hex()
        expected = f"{pmkid_hex}*{ap_hex}*{sta_hex}*{ssid_hex}"
        assert line == expected

    def test_format_has_three_stars(self):
        """hc22000 line must have exactly 3 '*' field separators."""
        line = format_hc22000(KNOWN_PMKID, KNOWN_AP_MAC, KNOWN_STA_MAC, KNOWN_SSID)
        assert line.count("*") == 3

    def test_format_pmkid_field_length(self):
        """PMKID hex field is exactly 32 chars (16 bytes × 2)."""
        line = format_hc22000(KNOWN_PMKID, KNOWN_AP_MAC, KNOWN_STA_MAC, KNOWN_SSID)
        pmkid_field = line.split("*")[0]
        assert len(pmkid_field) == 32

    def test_format_mac_field_length(self):
        """AP MAC and STA MAC fields are each 12 chars (6 bytes × 2, no colons)."""
        line = format_hc22000(KNOWN_PMKID, KNOWN_AP_MAC, KNOWN_STA_MAC, KNOWN_SSID)
        parts = line.split("*")
        assert len(parts[1]) == 12, "AP MAC field should be 12 hex chars"
        assert len(parts[2]) == 12, "STA MAC field should be 12 hex chars"

    def test_format_ssid_hex_correct(self):
        """SSID field is ASCII-encoded SSID as hex (not base64)."""
        line = format_hc22000(KNOWN_PMKID, KNOWN_AP_MAC, KNOWN_STA_MAC, KNOWN_SSID)
        ssid_field = line.split("*")[3]
        assert bytes.fromhex(ssid_field) == KNOWN_SSID

    def test_format_no_colons_in_macs(self):
        """MAC fields must not contain colons (hashcat expects raw hex)."""
        line = format_hc22000(KNOWN_PMKID, KNOWN_AP_MAC, KNOWN_STA_MAC, KNOWN_SSID)
        assert ":" not in line

    def test_format_lowercase_hex(self):
        """All hex digits should be lowercase (hashcat convention)."""
        line = format_hc22000(KNOWN_PMKID, KNOWN_AP_MAC, KNOWN_STA_MAC, KNOWN_SSID)
        hex_chars = line.replace("*", "")
        assert hex_chars == hex_chars.lower()

    def test_format_empty_ssid(self):
        """Hidden networks have empty SSID — should produce empty hex field."""
        line = format_hc22000(KNOWN_PMKID, KNOWN_AP_MAC, KNOWN_STA_MAC, b"")
        parts = line.split("*")
        assert parts[3] == ""

    def test_format_unicode_ssid(self):
        """Non-ASCII SSID bytes should round-trip through hex."""
        ssid = "CaféNet".encode("utf-8")
        line = format_hc22000(KNOWN_PMKID, KNOWN_AP_MAC, KNOWN_STA_MAC, ssid)
        parts = line.split("*")
        assert bytes.fromhex(parts[3]) == ssid


# ─── integration: extract → format pipeline ─────────────────────────────────

class TestExtractToHc22000Pipeline:

    def test_full_pipeline(self):
        """Extract PMKID from KDE, format to hc22000 — end-to-end."""
        kd = _make_kde_data(KNOWN_PMKID)
        pmkid = extract_pmkid(kd)
        assert pmkid is not None
        line = format_hc22000(pmkid, KNOWN_AP_MAC, KNOWN_STA_MAC, KNOWN_SSID)
        assert line.startswith(KNOWN_PMKID.hex())
        assert "*" + KNOWN_AP_MAC.hex() + "*" in line

    def test_hashcat_parseable(self):
        """Output line parses cleanly by splitting on '*'."""
        kd = _make_kde_data(KNOWN_PMKID)
        pmkid = extract_pmkid(kd)
        line = format_hc22000(pmkid, KNOWN_AP_MAC, KNOWN_STA_MAC, KNOWN_SSID)
        parts = line.split("*")
        assert len(parts) == 4
        # All fields are valid hex
        for i, part in enumerate(parts):
            if part:  # empty SSID is allowed
                bytes.fromhex(part)  # would raise if not valid hex
