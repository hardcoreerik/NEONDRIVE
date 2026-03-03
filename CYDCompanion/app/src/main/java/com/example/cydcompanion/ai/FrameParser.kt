package com.example.cydcompanion.ai

import com.example.cydcompanion.domain.EapolFrameInfo

/**
 * Stateless 802.11 / EAPOL byte-level parser.
 *
 * EAPOL/WPA Key frame layout (offsets within the EAPOL payload, not the 802.11 header):
 *   [0]       Key Descriptor Type  (0x02 = WPA/RSN)
 *   [1–2]     Key Information      (big-endian 16-bit flags)
 *   [3–4]     Key Length
 *   [5–12]    Replay Counter       (64-bit big-endian)
 *   [13–44]   Key Nonce            (32 bytes)
 *   [45–52]   EAPOL-Key IV         (8 bytes)
 *   [53–60]   Key RSC
 *   [61–68]   Reserved
 *   [69–84]   Key MIC              (16 bytes — zeroed in M1/M3 before MIC computation)
 *   [85–86]   Key Data Length
 *   [87+]     Key Data             (RSN IE, GTK wrapped, etc.)
 *
 * Key Information bit assignments (IEEE 802.11-2020 §12.7.2):
 *   Bits  0–2    Key Descriptor Version  (2=CCMP/SHA1, 3=CCMP-256/GCMP/SHA256)
 *   Bit   3      Key Type                (1=pairwise, 0=group)
 *   Bits  4–5    Key Index
 *   Bit   6      Install
 *   Bit   7      Key ACK                 (set in M1, M3)
 *   Bit   8      Key MIC                 (set in M2, M3, M4)
 *   Bit   9      Secure                  (set in M3, M4)
 *   Bit  10      Error
 *   Bit  11      Request
 *   Bit  12      Encrypted Key Data
 *   Bit  13      SMK Message
 *
 * 4-Way handshake message identification:
 *   M1: ACK=1, MIC=0, Pairwise=1, Secure=0
 *   M2: ACK=0, MIC=1, Pairwise=1, Secure=0
 *   M3: ACK=1, MIC=1, Pairwise=1, Secure=1
 *   M4: ACK=0, MIC=1, Pairwise=1, Secure=1
 */
object FrameParser {

    /**
     * Parse an EAPOL payload (hex-encoded) into an [EapolFrameInfo].
     * Returns null when the hex is too short or malformed.
     */
    fun parseEapol(
        messageNum: Int,
        eapolHex: String,
        frameHex: String,
        rssi: Int,
        timestampMs: Long
    ): EapolFrameInfo? {
        if (eapolHex.length < 10) return null
        val bytes = hexToBytes(eapolHex) ?: return null
        if (bytes.size < 5) return null

        val keyDescriptorType = bytes[0].toInt() and 0xFF
        val keyInfo = if (bytes.size >= 3) {
            ((bytes[1].toInt() and 0xFF) shl 8) or (bytes[2].toInt() and 0xFF)
        } else 0
        val keyLen = if (bytes.size >= 5) {
            ((bytes[3].toInt() and 0xFF) shl 8) or (bytes[4].toInt() and 0xFF)
        } else 0

        val replayCounter = if (bytes.size >= 13) {
            var rc = 0L
            for (i in 5..12) rc = (rc shl 8) or (bytes[i].toLong() and 0xFF)
            rc
        } else 0L

        val nonce = if (bytes.size >= 45) {
            bytes.slice(13..44).joinToString("") { "%02X".format(it) }
        } else ""

        val mic = if (bytes.size >= 85) {
            bytes.slice(69..84).joinToString("") { "%02X".format(it) }
        } else ""

        // Key Information flag extraction
        val hasMic         = (keyInfo and 0x0100) != 0  // bit 8
        val hasAck         = (keyInfo and 0x0080) != 0  // bit 7
        val isSecure       = (keyInfo and 0x0200) != 0  // bit 9
        val isPairwise     = (keyInfo and 0x0008) != 0  // bit 3
        val isEncryptedKD  = (keyInfo and 0x1000) != 0  // bit 12
        val keyDescVersion = keyInfo and 0x0007

        // Cipher hint from descriptor version:
        //   1 = TKIP (RC4+HMAC-MD5), 2 = CCMP (AES+HMAC-SHA1), 3 = CCMP-256/GCMP
        val cipherHint = when (keyDescVersion) {
            1    -> "TKIP"
            2    -> "CCMP/AES"
            3    -> "CCMP-256/GCMP"
            else -> "Unknown"
        }

        return EapolFrameInfo(
            messageNum     = messageNum,
            rssi           = rssi,
            timestampMs    = timestampMs,
            keyInfo        = keyInfo,
            keyLen         = keyLen,
            replayCounter  = replayCounter,
            nonce          = nonce,
            mic            = mic,
            hasMic         = hasMic,
            hasAck         = hasAck,
            isSecure       = isSecure,
            isPairwise     = isPairwise,
            isEncryptedKD  = isEncryptedKD,
            keyDescVersion = keyDescVersion,
            cipherHint     = cipherHint,
            keyDescriptorType = keyDescriptorType,
            eapolHex       = eapolHex,
            frameHex       = frameHex
        )
    }

    /** Decode uppercase/lowercase hex string to ByteArray. Returns null on error. */
    fun hexToBytes(hex: String): ByteArray? {
        if (hex.isEmpty() || hex.length % 2 != 0) return null
        return try {
            ByteArray(hex.length / 2) { i ->
                hex.substring(i * 2, i * 2 + 2).toInt(16).toByte()
            }
        } catch (_: NumberFormatException) {
            null
        }
    }

    /**
     * Extract the AP BSSID from the addr2 field of a raw 802.11 data/mgmt frame hex.
     * 802.11 header: [0-1]=FC, [2-3]=Duration, [4-9]=Addr1, [10-15]=Addr2, [16-21]=Addr3
     * addr2 is typically the transmitter (AP BSSID for AP→STA frames).
     */
    fun extractAddr2(frameHex: String): String? {
        val bytes = hexToBytes(frameHex) ?: return null
        if (bytes.size < 16) return null
        return bytes.slice(10..15).joinToString(":") { "%02X".format(it) }
    }

    /**
     * Extract source/destination/bssid MAC strings from a raw 802.11 frame hex.
     */
    fun extractAddrs(frameHex: String): Triple<String, String, String>? {
        val bytes = hexToBytes(frameHex) ?: return null
        if (bytes.size < 22) return null
        fun fmt(range: IntRange) = bytes.slice(range).joinToString(":") { "%02X".format(it) }
        return Triple(fmt(4..9), fmt(10..15), fmt(16..21))
    }
}
