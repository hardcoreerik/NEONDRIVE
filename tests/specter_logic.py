"""
SP3CTER reference implementation in Python.

Mirrors the C++ engine in src/main.cpp so that correctness can be verified
without hardware.  Tests import from this module directly.

EAPOL key_info bit layout (802.11i / IEEE 802.1X-2004, big-endian u16):
  bit 0     Key Descriptor Version (low)
  bit 1     Key Descriptor Version (high)
  bit 2     Key Type   (1 = Pairwise, 0 = Group)
  bit 3     Install
  bit 4     Key Ack
  bit 5     Key MIC
  bit 6     Secure
  bit 7     Error
  bit 8     Request
  bit 9     Encrypted Key Data
  bit 10    SMK Message
  bits 11-15 reserved
"""
from __future__ import annotations
import struct
from dataclasses import dataclass, field
from typing import Optional, List, Tuple


# ──────────────────────────────────────────────
#  EAPOL header offsets (after 802.11 + LLC/SNAP)
# ──────────────────────────────────────────────
EAPOL_ETHERTYPE = (0x88, 0x8E)

# key_info bits — matched to C++ specterClassifyEapol() masks in src/main.cpp.
# IEEE 802.11i key_info big-endian u16 layout:
#   bits 2-0: Key Descriptor Version
#   bit  3:   Key Type (pairwise=1)
#   bit  4-5: Reserved (Key Index / WPA legacy)
#   bit  6:   Install
#   bit  7:   Key Ack
#   bit  8:   Key MIC
#   bit  9:   Secure
#   bit 10:   Error
#   bit 11:   Request
#   bit 12:   Encrypted Key Data
_BIT_INSTALL = 0x0040  # bit 6
_BIT_ACK     = 0x0080  # bit 7
_BIT_MIC     = 0x0100  # bit 8
_BIT_SECURE  = 0x0200  # bit 9

# PMKID KDE OUI+type: 00 0F AC 04
_PMKID_KDE_HDR = bytes([0xDD, 0x14, 0x00, 0x0F, 0xAC, 0x04])
PMKID_LEN = 16


@dataclass
class EapolMsg:
    """Parsed EAPOL Key frame fields."""
    key_info: int          # raw 16-bit key_info field
    replay_counter: int    # 64-bit replay counter
    key_data: bytes        # variable-length key data field


def find_eapol_offset(frame: bytes) -> int:
    """Return byte offset of the EAPOL EtherType 0x88 0x8E, or -1."""
    for i in range(len(frame) - 1):
        if frame[i] == 0x88 and frame[i + 1] == 0x8E:
            return i
    return -1


def parse_eapol(frame: bytes) -> Optional[EapolMsg]:
    """
    Parse an 802.11 frame containing an EAPOL Key packet.

    Layout after EtherType (offset+2):
      version   1B
      type      1B  (must be 3 = Key)
      length    2B
      descriptor_type  1B
      key_info  2B  (big-endian)
      key_len   2B
      replay_counter  8B
      nonce    32B
      iv        16B
      rsc        8B
      id         8B
      mic       16B
      key_data_len  2B
      key_data  N bytes
    """
    off = find_eapol_offset(frame)
    if off < 0:
        return None
    off += 2  # skip EtherType

    # Minimum: version(1) + type(1) + length(2) + descriptor(1) + key_info(2)
    #          + key_len(2) + replay(8) + nonce(32) + iv(16) + rsc(8) + id(8)
    #          + mic(16) + key_data_len(2) = 99 bytes
    if off + 99 > len(frame):
        return None

    # version at off, type at off+1
    pkt_type = frame[off + 1]
    if pkt_type != 3:        # 3 = EAPOL-Key
        return None

    descriptor = frame[off + 4]
    if descriptor not in (1, 2):   # RC4 or AES key descriptor
        return None

    key_info = struct.unpack_from(">H", frame, off + 5)[0]
    # replay counter at offset 9 (after version+type+length+descriptor+key_info+key_len)
    replay_counter = struct.unpack_from(">Q", frame, off + 9)[0]

    key_data_len_off = off + 81  # 1+1+2+1+2+2+8+32+16+8+8+16 = 97, then 2 more = 97→99-2=97
    # Recount: version(1)+type(1)+length(2)+desc(1)+key_info(2)+key_len(2)+replay(8)
    #          +nonce(32)+iv(16)+rsc(8)+id(8)+mic(16) = 97 bytes → key_data_len at off+97
    key_data_len_off = off + 97
    if key_data_len_off + 2 > len(frame):
        return None
    key_data_len = struct.unpack_from(">H", frame, key_data_len_off)[0]
    kd_start = key_data_len_off + 2
    if kd_start + key_data_len > len(frame):
        return None
    key_data = frame[kd_start: kd_start + key_data_len]

    return EapolMsg(key_info=key_info, replay_counter=replay_counter, key_data=key_data)


# ──────────────────────────────────────────────
#  M1/M2/M3/M4 classification
# ──────────────────────────────────────────────

def classify_eapol(msg: EapolMsg) -> str:
    """
    Return "M1", "M2", "M3", "M4", or "UNKNOWN".

    C++ mirror of specterClassifyEapol():
        M1: Ack=1, MIC=0
        M2: Ack=0, MIC=1, Secure=0
        M3: Ack=1, MIC=1, Install=1
        M4: Ack=0, MIC=1, Secure=1
    """
    ki = msg.key_info
    ack     = bool(ki & _BIT_ACK)
    mic     = bool(ki & _BIT_MIC)
    secure  = bool(ki & _BIT_SECURE)
    install = bool(ki & _BIT_INSTALL)

    if ack and not mic:
        return "M1"
    if not ack and mic and not secure:
        return "M2"
    if ack and mic and install:
        return "M3"
    if not ack and mic and secure:
        return "M4"
    return "UNKNOWN"


# ──────────────────────────────────────────────
#  PMKID KDE extraction
# ──────────────────────────────────────────────

def extract_pmkid(key_data: bytes) -> Optional[bytes]:
    """
    Walk KDE list in key_data looking for OUI+type = 00 0F AC 04 with length 0x14.
    Return 16-byte PMKID or None.

    KDE format: DD LEN OUI(3B) TYPE(1B) DATA
    For PMKID: DD 14 00 0F AC 04 <16 bytes PMKID>
    """
    i = 0
    while i + 6 <= len(key_data):
        if (key_data[i] == 0xDD and
                key_data[i + 1] == 0x14 and
                key_data[i + 2] == 0x00 and
                key_data[i + 3] == 0x0F and
                key_data[i + 4] == 0xAC and
                key_data[i + 5] == 0x04):
            pmkid_start = i + 6
            if pmkid_start + PMKID_LEN <= len(key_data):
                return bytes(key_data[pmkid_start: pmkid_start + PMKID_LEN])
        # advance: KDE[i+1] is length of remaining (after DD + LEN byte itself)
        kde_len = key_data[i + 1] if i + 1 < len(key_data) else 0
        i += 2 + kde_len
    return None


# ──────────────────────────────────────────────
#  hc22000 output format
# ──────────────────────────────────────────────

def format_hc22000(pmkid: bytes, ap_mac: bytes, sta_mac: bytes, ssid: bytes) -> str:
    """
    Return hashcat hc22000 line:
      PMKID*AP_MAC*STA_MAC*SSID_HEX
    All bytes as lowercase hex, no separators within fields.
    """
    def hexb(b: bytes) -> str:
        return b.hex()

    return f"{hexb(pmkid)}*{hexb(ap_mac)}*{hexb(sta_mac)}*{hexb(ssid)}"


# ──────────────────────────────────────────────
#  Handshake completeness tracker
# ──────────────────────────────────────────────

@dataclass
class HsTrack:
    sta_mac: bytes
    m1: bool = False
    m2: bool = False
    m3: bool = False
    m4: bool = False

    def update(self, msg_num: str) -> None:
        if msg_num == "M1":
            self.m1 = True
        elif msg_num == "M2":
            self.m2 = True
        elif msg_num == "M3":
            self.m3 = True
        elif msg_num == "M4":
            self.m4 = True

    @property
    def is_complete(self) -> bool:
        return self.m1 and self.m2 and self.m3 and self.m4

    @property
    def completeness(self) -> int:
        return sum([self.m1, self.m2, self.m3, self.m4])


class HsTracker:
    """LRU table of up to max_size handshake tracks, keyed by STA MAC."""

    def __init__(self, max_size: int = 6):
        self._max = max_size
        self._tracks: List[HsTrack] = []
        self._ages: List[int] = []  # monotonic tick, higher = more recent
        self._tick = 0

    def _find(self, sta_mac: bytes) -> int:
        for i, t in enumerate(self._tracks):
            if t.sta_mac == sta_mac:
                return i
        return -1

    def record(self, sta_mac: bytes, msg_num: str) -> HsTrack:
        idx = self._find(sta_mac)
        if idx < 0:
            if len(self._tracks) >= self._max:
                # evict LRU
                lru = self._ages.index(min(self._ages))
                self._tracks.pop(lru)
                self._ages.pop(lru)
            self._tracks.append(HsTrack(sta_mac=sta_mac))
            self._ages.append(0)
            idx = len(self._tracks) - 1
        self._tick += 1
        self._ages[idx] = self._tick
        self._tracks[idx].update(msg_num)
        return self._tracks[idx]

    def get(self, sta_mac: bytes) -> Optional[HsTrack]:
        idx = self._find(sta_mac)
        return self._tracks[idx] if idx >= 0 else None

    @property
    def count(self) -> int:
        return len(self._tracks)


# ──────────────────────────────────────────────
#  Retransmission dedup
# ──────────────────────────────────────────────

@dataclass
class DedupEntry:
    mac: bytes
    replay_counter: int


class DedupRing:
    """Ring buffer dedup keyed on (Addr2 MAC, replay_counter)."""

    def __init__(self, size: int = 16):
        self._size = size
        self._ring: List[Optional[DedupEntry]] = [None] * size
        self._head = 0

    def is_duplicate(self, mac: bytes, replay_counter: int) -> bool:
        for entry in self._ring:
            if entry is not None and entry.mac == mac and entry.replay_counter == replay_counter:
                return True
        return False

    def add(self, mac: bytes, replay_counter: int) -> None:
        self._ring[self._head] = DedupEntry(mac=mac, replay_counter=replay_counter)
        self._head = (self._head + 1) % self._size


# ──────────────────────────────────────────────
#  Ghost frame construction helpers
# ──────────────────────────────────────────────

def build_auth_frame(src_mac: bytes, dst_mac: bytes, bssid: bytes) -> bytes:
    """
    Build a minimal 802.11 Open System Authentication Request frame.
    FC=0xB0 0x00, Duration=0, DA=dst, SA=src, BSSID=bssid,
    SeqCtrl=0, AuthAlgo=0 (Open), SeqNum=1, StatusCode=0
    """
    fc = bytes([0xB0, 0x00])
    duration = bytes([0x00, 0x00])
    seq_ctrl = bytes([0x00, 0x00])
    auth_algo = bytes([0x00, 0x00])
    auth_seq  = bytes([0x01, 0x00])
    status    = bytes([0x00, 0x00])
    return fc + duration + dst_mac + src_mac + bssid + seq_ctrl + auth_algo + auth_seq + status


def build_assoc_frame(src_mac: bytes, dst_mac: bytes, bssid: bytes, ssid: bytes) -> bytes:
    """
    Build an 802.11 Association Request with RSN IE (WPA2-CCMP-PSK, no PMKID list).
    """
    fc = bytes([0x00, 0x00])  # Association Request
    duration = bytes([0x00, 0x00])
    seq_ctrl = bytes([0x00, 0x00])
    cap_info = bytes([0x01, 0x00])  # ESS
    listen_interval = bytes([0x0A, 0x00])

    # SSID IE
    ssid_ie = bytes([0x00, len(ssid)]) + ssid

    # Supported Rates IE (minimal: 1 Mbps)
    rates_ie = bytes([0x01, 0x01, 0x82])

    # RSN IE: WPA2-CCMP group, WPA2-CCMP pairwise, PSK AKM, no PMKID list
    rsn_ie = bytes([
        0x30, 0x14,                         # Element ID, Length
        0x01, 0x00,                         # RSN Version
        0x00, 0x0F, 0xAC, 0x04,            # Group Cipher: CCMP
        0x01, 0x00,                         # Pairwise Cipher Count: 1
        0x00, 0x0F, 0xAC, 0x04,            # Pairwise: CCMP
        0x01, 0x00,                         # AKM Count: 1
        0x00, 0x0F, 0xAC, 0x02,            # AKM: PSK
        0x00, 0x00,                         # RSN Capabilities
    ])

    body = (cap_info + listen_interval + ssid_ie + rates_ie + rsn_ie)
    return fc + duration + dst_mac + src_mac + bssid + seq_ctrl + body


def is_locally_administered_unicast(mac: bytes) -> bool:
    """Check that MAC byte[0] has LA bit set (0x02) and multicast bit clear."""
    return (mac[0] & 0x03) == 0x02


# ──────────────────────────────────────────────
#  MAC address extraction from 802.11 data frames
# ──────────────────────────────────────────────

def extract_sta_mac_from_data(frame: bytes) -> Optional[bytes]:
    """
    Extract the STA (client) MAC from a data frame using ToDS/FromDS flags.
    Returns None if frame is too short or not a data frame.

    ToDS=1, FromDS=0: Addr2 is STA
    ToDS=0, FromDS=1: Addr1 is STA
    """
    if len(frame) < 26:
        return None
    fc0 = frame[0]
    fc1 = frame[1]
    frame_type = (fc0 >> 2) & 0x03
    if frame_type != 2:  # not data
        return None
    to_ds   = bool(fc1 & 0x01)
    from_ds = bool(fc1 & 0x02)
    if to_ds and not from_ds:
        # Addr2 = STA
        return bytes(frame[10:16])
    if not to_ds and from_ds:
        # Addr1 = STA
        return bytes(frame[4:10])
    return None
