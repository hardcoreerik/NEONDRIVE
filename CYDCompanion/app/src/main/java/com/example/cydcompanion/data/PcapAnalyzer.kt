package com.example.cydcompanion.data

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.RandomAccessFile

data class PcapAnalysis(
    val fileName: String,
    val fileSize: Long,
    val packetCount: Int,
    val hasEapol: Boolean,
    val eapolCount: Int,
    val bssids: Set<String>,
    val ssids: Set<String>,
    val captureType: String, // "WPA Handshake", "Deauth", "Raw", "Unknown"
    val summary: String
)

/**
 * Basic PCAP analyzer for quick statistics
 * Reads .pcap files to extract WiFi handshake info
 */
class PcapAnalyzer {
    
    companion object {
        private const val PCAP_MAGIC_LE = 0xa1b2c3d4.toInt()
        private const val PCAP_MAGIC_BE = 0xd4c3b2a1.toInt()
        private const val PCAP_MAGIC_NANOS_LE = 0xa1b23c4d.toInt()
        private const val PCAP_MAGIC_NANOS_BE = 0x4d3cb2a1.toInt()
        
        private const val LINKTYPE_IEEE802_11 = 105
        private const val LINKTYPE_IEEE802_11_RADIOTAP = 127
        
        // 802.11 frame types
        private const val TYPE_MANAGEMENT = 0
        private const val TYPE_DATA = 2
        
        // EAPOL (WPA handshake) marker
        private val EAPOL_MARKER = byteArrayOf(0x88.toByte(), 0x8e.toByte())
    }
    
    suspend fun analyze(file: File): PcapAnalysis = withContext(Dispatchers.IO) {
        if (!file.exists() || file.length() < 24) {
            return@withContext PcapAnalysis(
                fileName = file.name,
                fileSize = file.length(),
                packetCount = 0,
                hasEapol = false,
                eapolCount = 0,
                bssids = emptySet(),
                ssids = emptySet(),
                captureType = "Invalid",
                summary = "File too small or missing"
            )
        }
        
        try {
            RandomAccessFile(file, "r").use { raf ->
                // Read global header (24 bytes)
                val magic = raf.readInt()
                val littleEndian = when (magic) {
                    PCAP_MAGIC_LE, PCAP_MAGIC_NANOS_LE -> true
                    PCAP_MAGIC_BE, PCAP_MAGIC_NANOS_BE -> false
                    else -> {
                        return@withContext PcapAnalysis(
                            fileName = file.name,
                            fileSize = file.length(),
                            packetCount = 0,
                            hasEapol = false,
                            eapolCount = 0,
                            bssids = emptySet(),
                            ssids = emptySet(),
                            captureType = "Invalid PCAP",
                            summary = "Not a valid PCAP file"
                        )
                    }
                }
                
                // Skip version, timezone, sigfigs, snaplen
                raf.skipBytes(16)
                val linkType = readInt(raf, littleEndian)
                
                if (linkType != LINKTYPE_IEEE802_11 && linkType != LINKTYPE_IEEE802_11_RADIOTAP) {
                    return@withContext PcapAnalysis(
                        fileName = file.name,
                        fileSize = file.length(),
                        packetCount = 0,
                        hasEapol = false,
                        eapolCount = 0,
                        bssids = emptySet(),
                        ssids = emptySet(),
                        captureType = "Non-WiFi",
                        summary = "Not a WiFi capture (link type: $linkType)"
                    )
                }
                
                var packetCount = 0
                var eapolCount = 0
                val bssids = mutableSetOf<String>()
                
                // Read packets
                while (raf.filePointer < raf.length() - 16) {
                    try {
                        // Packet header: ts_sec(4) ts_usec(4) incl_len(4) orig_len(4)
                        raf.skipBytes(8) // timestamp
                        val inclLen = readInt(raf, littleEndian)
                        raf.skipBytes(4) // orig_len
                        
                        if (inclLen <= 0 || inclLen > 65535) break
                        
                        val packetData = ByteArray(inclLen)
                        val bytesRead = raf.read(packetData)
                        if (bytesRead != inclLen) break
                        
                        packetCount++
                        
                        // Check for EAPOL
                        if (containsEapol(packetData)) {
                            eapolCount++
                        }
                        
                        // Extract BSSID (if present)
                        extractBssid(packetData, linkType)?.let { bssids.add(it) }
                        
                        // Limit analysis to first 1000 packets for speed
                        if (packetCount >= 1000) break
                        
                    } catch (e: Exception) {
                        break
                    }
                }
                
                val captureType = when {
                    eapolCount >= 4 -> "WPA Handshake (Complete)"
                    eapolCount > 0 -> "WPA Handshake (Partial: $eapolCount/4)"
                    packetCount > 0 -> "WiFi Traffic"
                    else -> "Empty"
                }
                
                val summary = buildString {
                    append("$packetCount packets")
                    if (eapolCount > 0) {
                        append(", $eapolCount EAPOL frames")
                    }
                    if (bssids.isNotEmpty()) {
                        append(", ${bssids.size} AP(s)")
                    }
                }
                
                PcapAnalysis(
                    fileName = file.name,
                    fileSize = file.length(),
                    packetCount = packetCount,
                    hasEapol = eapolCount > 0,
                    eapolCount = eapolCount,
                    bssids = bssids,
                    ssids = emptySet(), // SSID extraction requires more parsing
                    captureType = captureType,
                    summary = summary
                )
            }
        } catch (e: Exception) {
            PcapAnalysis(
                fileName = file.name,
                fileSize = file.length(),
                packetCount = 0,
                hasEapol = false,
                eapolCount = 0,
                bssids = emptySet(),
                ssids = emptySet(),
                captureType = "Error",
                summary = "Analysis failed: ${e.message}"
            )
        }
    }
    
    private fun readInt(raf: RandomAccessFile, littleEndian: Boolean): Int {
        val bytes = ByteArray(4)
        raf.readFully(bytes)
        return if (littleEndian) {
            ((bytes[3].toInt() and 0xFF) shl 24) or
            ((bytes[2].toInt() and 0xFF) shl 16) or
            ((bytes[1].toInt() and 0xFF) shl 8) or
            (bytes[0].toInt() and 0xFF)
        } else {
            ((bytes[0].toInt() and 0xFF) shl 24) or
            ((bytes[1].toInt() and 0xFF) shl 16) or
            ((bytes[2].toInt() and 0xFF) shl 8) or
            (bytes[3].toInt() and 0xFF)
        }
    }
    
    private fun containsEapol(packet: ByteArray): Boolean {
        for (i in 0 until packet.size - 1) {
            if (packet[i] == EAPOL_MARKER[0] && packet[i + 1] == EAPOL_MARKER[1]) {
                return true
            }
        }
        return false
    }
    
    private fun extractBssid(packet: ByteArray, linkType: Int): String? {
        try {
            var offset = 0
            
            // Skip RadioTap header if present
            if (linkType == LINKTYPE_IEEE802_11_RADIOTAP) {
                if (packet.size < 4) return null
                val rtLen = ((packet[3].toInt() and 0xFF) shl 8) or (packet[2].toInt() and 0xFF)
                offset = rtLen
            }
            
            // 802.11 frame starts at offset
            if (packet.size < offset + 24) return null
            
            // BSSID is at different positions depending on frame type
            // For simplicity, extract address 3 (usually BSSID)
            val bssidOffset = offset + 16
            if (packet.size < bssidOffset + 6) return null
            
            val bssid = (0 until 6).joinToString(":") { i ->
                "%02X".format(packet[bssidOffset + i])
            }
            
            // Filter out broadcast/multicast
            if (bssid.startsWith("FF:FF:FF") || (packet[bssidOffset].toInt() and 0x01) == 1) {
                return null
            }
            
            return bssid
        } catch (e: Exception) {
            return null
        }
    }
}
