package com.example.cydcompanion.ai

import android.content.ContentValues
import android.content.Context
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import com.example.cydcompanion.domain.EapolFrameInfo
import com.example.cydcompanion.domain.TargetProfile
import java.io.File
import java.io.FileWriter

/**
 * Exports captured WPA handshakes in formats compatible with offline cracking tools.
 *
 * Supported formats:
 *   - HC22000 / hcwpax (.22000)  — hashcat's current capture format (text-based)
 *   - hccapx (.hccapx)           — legacy binary format (hashcat ≤ 5.x)
 *
 * HC22000 line format (MIC-based):
 *   WPA*02*{MIC}*{MACAP}*{MACSTA}*{ESSID_HEX}*{ANONCE}*{EAPOL}*{MESSAGEPAIR}
 *
 * Reference: https://hashcat.net/wiki/doku.php?id=hccapx
 */
object HandshakeExporter {

    data class ExportResult(
        val lines: List<String>,        // HC22000 line(s) — one per valid M1+M2 pair
        val filePath: String? = null,   // set if saved to disk
        val error: String? = null
    )

    // ------------------------------------------------------------------
    // HC22000 text export
    // ------------------------------------------------------------------

    /**
     * Convert [profile] to hashcat HC22000 lines.
     * Requires M1 (ANonce) + M2 (SNonce, MIC, EAPOL) to be captured.
     *
     * Returns a list of WPA*02* lines suitable for direct use with:
     *   hashcat -m 22000 capture.22000 wordlist.txt
     */
    fun toHc22000(profile: TargetProfile): ExportResult {
        val m1 = profile.frames.firstOrNull { it.messageNum == 1 }
        val m2 = profile.frames.firstOrNull { it.messageNum == 2 }

        if (m2 == null) return ExportResult(
            emptyList(), error = "M2 not captured — MIC unavailable"
        )
        if (m1 == null && m2.nonce.all { it == '0' }) return ExportResult(
            emptyList(), error = "ANonce not available (need M1 or M3)"
        )

        val aNonce = (m1?.nonce ?: profile.frames.firstOrNull { it.messageNum == 3 }?.nonce)
            ?: return ExportResult(emptyList(), error = "ANonce not found")
        val apMac  = profile.bssid.replace(":", "").lowercase()
        val staMac = (profile.clientMac ?: "ffffffffffff").replace(":", "").lowercase()
        val mic    = m2.mic.lowercase().padEnd(32, '0').take(32)
        val essid  = profile.ssid.toByteArray(Charsets.UTF_8).joinToString("") { "%02x".format(it) }
        val eapol  = m2.eapolHex.lowercase()

        // messagepair: 0x02 = M1+M2, 0x04 = if AP-less (PMKID from M1 clientside)
        val msgPair = "02"

        val line = "WPA*02*$mic*$apMac*$staMac*$essid*${aNonce.lowercase()}*$eapol*$msgPair"
        return ExportResult(listOf(line))
    }

    /**
     * Write HC22000 export to the Downloads folder.
     * File name: cyd_<BSSID_no_colons>_<timestamp>.22000
     * On API 29+ uses MediaStore; on older APIs writes to Environment.DOWNLOADS directly.
     */
    fun saveHc22000(context: Context, profile: TargetProfile): ExportResult {
        val result = toHc22000(profile)
        if (result.error != null || result.lines.isEmpty()) return result

        val filename = "cyd_${profile.bssid.replace(":","")}_${System.currentTimeMillis()}.22000"
        val content  = result.lines.joinToString("\n") + "\n"

        return try {
            val path = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                saveViaMediaStore(context, filename, content, "text/plain")
            } else {
                saveToDownloads(filename, content)
            }
            result.copy(filePath = path)
        } catch (e: Exception) {
            result.copy(error = "Save failed: ${e.message}")
        }
    }

    // ------------------------------------------------------------------
    // hccapx binary export (legacy — hashcat ≤ 5.x / hashcat-utils)
    // ------------------------------------------------------------------

    /**
     * Build a minimal hccapx binary blob for [profile].
     * hccapx is a fixed-length per-record binary format:
     *   signature (4) + version (4) + apMac (6) + staMac (6) + essid_len (1)
     *   + essid (36 padded) + messagePair (1) + keyver (1) + keymic (16)
     *   + eapol_size (2 LE) + eapol (256 padded) = 393 bytes
     */
    fun toHccapx(profile: TargetProfile): ByteArray? {
        val m1 = profile.frames.firstOrNull { it.messageNum == 1 }
        val m2 = profile.frames.firstOrNull { it.messageNum == 2 } ?: return null

        val apMac    = hexToBytes(profile.bssid.replace(":", ""))
        val staMac   = hexToBytes((profile.clientMac ?: "FFFFFFFFFFFF").replace(":", ""))
        val essid    = profile.ssid.toByteArray(Charsets.UTF_8)
        val keyMic   = hexToBytes(m2.mic.padEnd(32, '0').take(32))
        val eapolRaw = hexToBytes(m2.eapolHex)
        val eapolSize = eapolRaw.size.coerceAtMost(256)
        val keyver   = m2.keyDescVersion.toByte()
        val aNonce   = hexToBytes(m1?.nonce ?: "0".repeat(64))
        val sNonce   = hexToBytes(m2.nonce)

        val out = java.io.ByteArrayOutputStream(393)
        // signature "HCPX"
        out.write(byteArrayOf(0x48, 0x43, 0x50, 0x58))
        // version 4 (LE)
        out.write(intToLeBytes(4, 4))
        // mac_ap (6)
        out.write(apMac.copyOf(6))
        // mac_sta (6)
        out.write(staMac.copyOf(6))
        // essid_len (1)
        out.write(essid.size.coerceAtMost(32))
        // essid (36 bytes, zero-padded)
        val essidPad = ByteArray(36)
        System.arraycopy(essid, 0, essidPad, 0, essid.size.coerceAtMost(32))
        out.write(essidPad)
        // message_pair (1): 0=M1+M2, 2=M2+M3
        out.write(0)
        // keyver (1)
        out.write(keyver.toInt())
        // keymic (16)
        out.write(keyMic.copyOf(16))
        // eapol_size (2 LE)
        out.write(intToLeBytes(eapolSize, 2))
        // eapol (256 bytes, zero-padded)
        val eapolPad = ByteArray(256)
        System.arraycopy(eapolRaw, 0, eapolPad, 0, eapolSize)
        out.write(eapolPad)
        // anonce (32)
        out.write(aNonce.copyOf(32))
        // snonce (32)
        out.write(sNonce.copyOf(32))

        return out.toByteArray()
    }

    // ------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------

    private fun intToLeBytes(value: Int, width: Int): ByteArray =
        ByteArray(width) { i -> ((value shr (8 * i)) and 0xFF).toByte() }

    private fun hexToBytes(hex: String): ByteArray {
        val clean = hex.replace(" ", "").replace(":", "")
        return ByteArray(clean.length / 2) { i ->
            clean.substring(i * 2, i * 2 + 2).toInt(16).toByte()
        }
    }

    private fun saveViaMediaStore(
        context: Context,
        filename: String,
        content: String,
        mimeType: String
    ): String {
        val values = ContentValues().apply {
            put(MediaStore.Downloads.DISPLAY_NAME, filename)
            put(MediaStore.Downloads.MIME_TYPE, mimeType)
            put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS)
        }
        val resolver = context.contentResolver
        val uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
            ?: throw Exception("MediaStore insert returned null")
        resolver.openOutputStream(uri)?.use { it.write(content.toByteArray()) }
        return uri.toString()
    }

    @Suppress("DEPRECATION")
    private fun saveToDownloads(filename: String, content: String): String {
        val dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
        dir.mkdirs()
        val file = File(dir, filename)
        FileWriter(file).use { it.write(content) }
        return file.absolutePath
    }
}
