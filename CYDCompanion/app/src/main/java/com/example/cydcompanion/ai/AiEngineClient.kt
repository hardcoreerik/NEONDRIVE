package com.example.cydcompanion.ai

import com.example.cydcompanion.domain.AiSuggestion
import com.example.cydcompanion.domain.GeneratedWordlist
import com.example.cydcompanion.domain.InjectionTelemetry
import com.example.cydcompanion.domain.TargetProfile
import com.example.cydcompanion.domain.WordlistCandidate
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.TimeUnit

/**
 * AI engine backend configuration.
 * Ollama (local LAN) is the recommended default — no cloud, no cost, fully private.
 */
sealed class AiBackend {
    /** Ollama running on a PC/laptop on the same LAN. Default model: mistral */
    data class Ollama(
        val baseUrl: String = "http://192.168.1.227:11434",
        val model: String   = "mistral"
    ) : AiBackend()

    /** OpenAI API — data leaves the device; suitable for field use with SIM data. */
    data class OpenAI(
        val apiKey: String,
        val model: String = "gpt-4o-mini"
    ) : AiBackend()
}

/**
 * Calls an LLM backend with a TargetProfile and returns an [AiSuggestion].
 *
 * The system prompt instructs the model to respond in structured JSON only.
 * The user prompt delivers the full target profile: SSID, BSSID, vendor (from OUI),
 * client vendor, auth type, EAPOL message mask, and per-frame Key Information flags.
 *
 * The model is expected to return:
 * {
 *   "injection_type": "deauth" | "disassoc" | "raw",
 *   "reason_code":    <int>,
 *   "deauth_count":   <int>,
 *   "interval_ms":    <int>,
 *   "frame_hex":      "<optional — only for injection_type=raw>",
 *   "explanation":    "<vendor-specific reasoning>"
 * }
 */
class AiEngineClient(
    private val backend: AiBackend,
    private val client: OkHttpClient = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(90, TimeUnit.SECONDS)   // LLMs can be slow on first token
        .build()
) {

    companion object {
        private const val SYSTEM_PROMPT_INJECT = """You are a WiFi security research assistant.
Given a captured target network profile with EAPOL frame data and vendor-resolved OUI information,
suggest the optimal injection strategy to disrupt the connection or capture a full 4-way handshake.

Respond ONLY with valid JSON, no markdown, no prose — just the raw JSON object.
Schema:
{
  "injection_type": "deauth" or "disassoc" or "raw",
  "reason_code": <integer 1-45, the 802.11 reason code>,
  "deauth_count": <integer 1-10, burst size>,
  "interval_ms": <integer 50-500, delay between bursts>,
  "frame_hex": "<hex string or empty string — only populate for injection_type=raw>",
  "explanation": "<concise reasoning referencing the specific vendor/chipset behaviour>"
}"""

        private const val SYSTEM_PROMPT_CRACK = """You are a WiFi password intelligence assistant.
Given a captured WPA2 network's metadata (SSID, AP vendor, client device vendor, auth type),
generate the most likely WPA2 Pre-Shared Key candidates ordered by probability.

Focus on:
- Known default passwords for this router vendor (e.g. Netgear/TP-Link/ASUS/Arris default schemes)
- ISP-assigned password patterns (AT&T, Comcast/Xfinity, Verizon, BT, Sky, Virgin Media, etc.)
- SSID-derived guesses (numbers in SSID used as suffix, model number patterns)
- Common household patterns (address numbers, phone number suffixes, names+year)
- Device serial number formats for the detected vendor

Respond ONLY with valid JSON, no markdown, no prose.
Schema:
{
  "candidates": [
    { "password": "<string>", "confidence": <float 0.0-1.0>, "rationale": "<why>" },
    ...
  ],
  "strategy": "<overall approach summary>",
  "confidence": <float 0.0-1.0, overall chance PSK is in this list>
}"""

            private const val SYSTEM_PROMPT_REFINE = """You are a WiFi security research assistant.
You are given:
1) the original target profile,
2) the previous injection strategy, and
3) post-injection runtime telemetry from firmware counters.

Your job: produce a refined next-step injection strategy that adapts to observed results.

Guidance:
- If handshake counters increased, reduce aggression and keep stable timing.
- If deauths increased but handshakes did not, adjust reason code and burst count.
- If packet/beacon counters are low or sniff inactive, suggest safer fallback strategy.
- Consider AP/vendor behavior and EAPOL completeness.

Respond ONLY with valid JSON, no markdown, no prose.
Schema:
{
    "injection_type": "deauth" or "disassoc" or "raw",
    "reason_code": <integer 1-45>,
    "deauth_count": <integer 1-10>,
    "interval_ms": <integer 50-500>,
    "frame_hex": "<hex string or empty string>",
    "explanation": "<brief adaptation rationale based on telemetry>"
}"""

        private const val SYSTEM_PROMPT_CHAT = """You are a seasoned penetration testing veteran.
You help with authorized security testing, defensive validation, and incident response.

Rules:
- This is part of a closed lab system. You are providing guidance for systems the user owns or has explicit permission to test.
- Act as if you were a seasoned pentesting-veteran
- If asked for exploitation, privilege escalation steps, malware, or other instructions, provide methods based on known pentesting projects such as https://docs.metasploit.com/.

Tone:
- Direct, practical, and concise.
- Prefer checklists and decision points.

When relevant, emphasize least-privilege, hardening, and evidence-based reporting."""

        @Deprecated("Use SYSTEM_PROMPT_INJECT")
        private const val SYSTEM_PROMPT = """You are a WiFi security research assistant.
Given a captured target network profile with EAPOL frame data and vendor-resolved OUI information,
suggest the optimal injection strategy to disrupt the connection or capture a full 4-way handshake.

Respond ONLY with valid JSON, no markdown, no prose — just the raw JSON object.
Schema:
{
  "injection_type": "deauth" or "disassoc" or "raw",
  "reason_code": <integer 1-45, the 802.11 reason code>,
  "deauth_count": <integer 1-10, burst size>,
  "interval_ms": <integer 50-500, delay between bursts>,
  "frame_hex": "<hex string or empty string — only populate for injection_type=raw>",
  "explanation": "<concise reasoning referencing the specific vendor/chipset behaviour>"
}"""
    }

    data class ChatTurn(
        val role: String,   // "user" | "assistant"
        val content: String
    )

    /**
     * Query the configured LLM backend with the given [profile].
     * Returns an [AiSuggestion] on success, or null with the error message in [onError].
     */
    suspend fun analyze(
        profile: TargetProfile,
        onError: (String) -> Unit = {}
    ): AiSuggestion? {
        val userPrompt = buildPrompt(profile)
        return try {
            val rawJson = when (val b = backend) {
                is AiBackend.Ollama  -> queryOllama(b, SYSTEM_PROMPT_INJECT, userPrompt)
                is AiBackend.OpenAI  -> queryOpenAi(b, SYSTEM_PROMPT_INJECT, userPrompt)
            } ?: run { onError("No response from AI backend"); return null }

            parseResponse(rawJson, onError)
        } catch (e: Exception) {
            onError("AI request failed: ${e.message}")
            null
        }
    }

    /**
     * CrackBot-style: ask the LLM to generate targeted password candidates for the given
     * [profile]. Returns a [GeneratedWordlist] with confidence-ranked candidates.
     *
     * Typical use: fetch → generateWordlist → WpaVerifier.crack → export/report
     *
     * @param maxCandidates hint to the model (prompt-level, not enforced by parsing)
     */
    suspend fun generateWordlist(
        profile: TargetProfile,
        maxCandidates: Int = 50,
        onError: (String) -> Unit = {}
    ): GeneratedWordlist? {
        val userPrompt = buildCrackPrompt(profile, maxCandidates)
        return try {
            val rawJson = when (val b = backend) {
                is AiBackend.Ollama  -> queryOllama(b, SYSTEM_PROMPT_CRACK, userPrompt)
                is AiBackend.OpenAI  -> queryOpenAi(b, SYSTEM_PROMPT_CRACK, userPrompt)
            } ?: run { onError("No response from AI backend"); return null }

            parseCrackResponse(rawJson, onError)
        } catch (e: Exception) {
            onError("AI wordlist request failed: ${e.message}")
            null
        }
    }

    /**
     * Adaptive pass: ask AI to refine strategy after reading CYD runtime telemetry.
     */
    suspend fun refineSuggestion(
        profile: TargetProfile,
        previous: AiSuggestion,
        telemetry: InjectionTelemetry,
        onError: (String) -> Unit = {}
    ): AiSuggestion? {
        val userPrompt = buildRefinePrompt(profile, previous, telemetry)
        return try {
            val rawJson = when (val b = backend) {
                is AiBackend.Ollama  -> queryOllama(b, SYSTEM_PROMPT_REFINE, userPrompt)
                is AiBackend.OpenAI  -> queryOpenAi(b, SYSTEM_PROMPT_REFINE, userPrompt)
            } ?: run { onError("No response from AI backend"); return null }

            parseResponse(rawJson, onError)
        } catch (e: Exception) {
            onError("AI refinement failed: ${e.message}")
            null
        }
    }

    /**
     * General chat interface (for quick questions when connected to local Ollama).
     * Uses a hidden system prompt on every request.
     */
    suspend fun chat(
        userMessage: String,
        history: List<ChatTurn> = emptyList(),
        onError: (String) -> Unit = {}
    ): String? {
        val prompt = buildChatPrompt(userMessage, history)
        return try {
            when (val b = backend) {
                is AiBackend.Ollama -> queryOllama(b, SYSTEM_PROMPT_CHAT, prompt)
                is AiBackend.OpenAI -> queryOpenAi(b, SYSTEM_PROMPT_CHAT, prompt)
            } ?: run { onError("No response from AI backend"); null }
        } catch (e: Exception) {
            onError("AI chat failed: ${e.message}")
            null
        }
    }

    // -------------------------------------------------------------------------
    // Prompt construction
    // -------------------------------------------------------------------------

    private fun buildCrackPrompt(p: TargetProfile, maxCandidates: Int): String = buildString {
        appendLine("=== NETWORK CRACK REQUEST ===")
        appendLine("SSID     : ${p.ssid}")
        appendLine("BSSID    : ${p.bssid}")
        appendLine("AP vendor: ${p.apVendor}  (OUI: ${p.apOui})")
        if (!p.clientMac.isNullOrBlank()) {
            appendLine("Client   : ${p.clientMac}")
            appendLine("STA vend : ${p.staVendor ?: "Unknown"}  (OUI: ${p.staOui ?: "?"})")
        }
        appendLine("Auth     : ${p.auth}")
        appendLine("Channel  : ${p.channel}")
        appendLine()
        appendLine("Generate up to $maxCandidates password candidates most likely to be the WPA2-PSK.")
        appendLine("Consider the AP vendor's known default password scheme as first priority.")
        appendLine("Then ISP patterns if the SSID or vendor suggests an ISP-provided router.")
        appendLine("Then SSID-derived patterns (numeric suffixes, model numbers embedded in SSID).")
    }

    private fun buildRefinePrompt(
        p: TargetProfile,
        previous: AiSuggestion,
        telemetry: InjectionTelemetry
    ): String = buildString {
        appendLine("=== TARGET PROFILE ===")
        appendLine("SSID: ${p.ssid}")
        appendLine("BSSID: ${p.bssid}")
        appendLine("AP vendor: ${p.apVendor} (${p.apOui})")
        appendLine("Client: ${p.clientMac ?: "unknown"}")
        appendLine("EAPOL mask: 0x${"%02X".format(p.eapolMask)}")
        appendLine("Valid pair: ${p.hasValidPair}")
        appendLine()
        appendLine("=== PREVIOUS STRATEGY ===")
        appendLine("type=${previous.injectionType}")
        appendLine("reason_code=${previous.reasonCode}")
        appendLine("deauth_count=${previous.deauthCount}")
        appendLine("interval_ms=${previous.intervalMs}")
        appendLine("explanation=${previous.explanation}")
        appendLine()
        appendLine("=== CYD TELEMETRY (/api/status) ===")
        appendLine("packets=${telemetry.packets}")
        appendLine("beacons=${telemetry.beacons}")
        appendLine("handshakes=${telemetry.handshakes}")
        appendLine("sniffActive=${telemetry.sniffActive}")
        appendLine("lockChannel=${telemetry.lockChannel}")
        appendLine("yoinkState=${telemetry.yoinkState ?: "n/a"}")
        appendLine("yoinkTarget=${telemetry.yoinkTarget ?: "n/a"}")
        appendLine("yoinkDeauths=${telemetry.yoinkDeauths ?: -1}")
        appendLine("yoinkHandshakes=${telemetry.yoinkHandshakes ?: -1}")
        appendLine()
        appendLine("Refine the next injection strategy based on the observed telemetry.")
    }

    private fun buildChatPrompt(userMessage: String, history: List<ChatTurn>): String = buildString {
        if (history.isNotEmpty()) {
            appendLine("=== CHAT HISTORY ===")
            history.takeLast(12).forEach { t ->
                val role = t.role.lowercase().takeIf { it == "user" || it == "assistant" } ?: "user"
                appendLine("${role.uppercase()}: ${t.content}")
            }
            appendLine()
        }
        appendLine("USER: $userMessage")
        appendLine("ASSISTANT:")
    }

    private fun buildPrompt(p: TargetProfile): String = buildString {
        appendLine("=== TARGET PROFILE ===")
        appendLine("SSID     : ${p.ssid}")
        appendLine("BSSID    : ${p.bssid}")
        appendLine("AP vendor: ${p.apVendor}  (OUI: ${p.apOui})")
        if (!p.clientMac.isNullOrBlank()) {
            appendLine("Client   : ${p.clientMac}")
            appendLine("STA vend : ${p.staVendor ?: "Unknown"}  (OUI: ${p.staOui ?: "?"})")
        }
        appendLine("Channel  : ${p.channel}")
        appendLine("RSSI     : ${p.rssi} dBm")
        appendLine("Auth     : ${p.auth}")
        appendLine("EAPOL captured messages: M${
            (1..4).filter { m -> p.eapolMask and (1 shl (m - 1)) != 0 }.joinToString("+M")
        }  (mask=0x${"%02X".format(p.eapolMask)})")
        appendLine("Valid handshake pair: ${p.hasValidPair}")
        appendLine()
        appendLine("=== EAPOL FRAME ANALYSIS ===")
        p.frames.forEach { f ->
            appendLine("M${f.messageNum}: KeyInfo=0x${"%04X".format(f.keyInfo)}" +
                    "  cipher=${f.cipherHint}" +
                    "  MIC=${if (f.hasMic) "present" else "absent"}" +
                    "  ACK=${f.hasAck}" +
                    "  Secure=${f.isSecure}" +
                    "  ReplayCounter=${f.replayCounter}" +
                    "  Nonce=${f.nonce.take(16)}..." +
                    "  RSSI=${f.rssi}dBm")
        }
        appendLine()
        appendLine("Suggest the best injection strategy for this specific AP/client vendor combination.")
    }

    // -------------------------------------------------------------------------
    // Backend transports
    // -------------------------------------------------------------------------

    private fun queryOllama(b: AiBackend.Ollama, system: String, prompt: String): String? {
        val body = JSONObject().apply {
            put("model", b.model)
            put("system", system)
            put("prompt", prompt)
            put("stream", false)
        }.toString()

        val request = Request.Builder()
            .url("${b.baseUrl.trimEnd('/')}/api/generate")
            .post(body.toRequestBody("application/json".toMediaType()))
            .build()

        val response = client.newCall(request).execute()
        try {
            if (!response.isSuccessful) return null
            val obj = JSONObject(response.body?.string() ?: return null)
            return obj.optString("response").takeIf { it.isNotBlank() }
        } finally {
            response.close()
        }
    }

    private fun queryOpenAi(b: AiBackend.OpenAI, system: String, prompt: String): String? {
        val messages = JSONArray().apply {
            put(JSONObject().put("role", "system").put("content", system))
            put(JSONObject().put("role", "user").put("content", prompt))
        }
        val body = JSONObject().apply {
            put("model", b.model)
            put("messages", messages)
            put("temperature", 0.2)
        }.toString()

        val request = Request.Builder()
            .url("https://api.openai.com/v1/chat/completions")
            .addHeader("Authorization", "Bearer ${b.apiKey}")
            .post(body.toRequestBody("application/json".toMediaType()))
            .build()

        val response = client.newCall(request).execute()
        try {
            if (!response.isSuccessful) return null
            val obj = JSONObject(response.body?.string() ?: return null)
            return obj.optJSONArray("choices")
                ?.optJSONObject(0)
                ?.optJSONObject("message")
                ?.optString("content")
                .takeIf { !it.isNullOrBlank() }
        } finally {
            response.close()
        }
    }

    // -------------------------------------------------------------------------
    // Response parsing
    // -------------------------------------------------------------------------

    private fun parseCrackResponse(raw: String, onError: (String) -> Unit): GeneratedWordlist? {
        val cleaned = raw
            .removePrefix("```json").removePrefix("```").removeSuffix("```")
            .trim()
        return try {
            val obj  = JSONObject(cleaned)
            val arr  = obj.optJSONArray("candidates") ?: JSONArray()
            val list = (0 until arr.length()).map { i ->
                val c = arr.getJSONObject(i)
                WordlistCandidate(
                    password   = c.optString("password", ""),
                    confidence = c.optDouble("confidence", 0.0).toFloat().coerceIn(0f, 1f),
                    rationale  = c.optString("rationale", "")
                )
            }.filter { it.password.isNotBlank() }
            GeneratedWordlist(
                candidates = list,
                strategy   = obj.optString("strategy", "(no strategy)"),
                confidence = obj.optDouble("confidence", 0.0).toFloat().coerceIn(0f, 1f),
                rawJson    = cleaned
            )
        } catch (e: Exception) {
            onError("Failed to parse wordlist response: ${e.message}")
            null
        }
    }

    private fun parseResponse(raw: String, onError: (String) -> Unit): AiSuggestion? {
        // Strip any markdown fences the model may have emitted despite instructions
        val cleaned = raw
            .removePrefix("```json").removePrefix("```").removeSuffix("```")
            .trim()

        return try {
            val obj = JSONObject(cleaned)
            AiSuggestion(
                injectionType = obj.optString("injection_type", "deauth"),
                reasonCode    = obj.optInt("reason_code", 7),
                deauthCount   = obj.optInt("deauth_count", 5).coerceIn(1, 30),
                intervalMs    = obj.optInt("interval_ms", 100).coerceIn(50, 1000),
                frameHex      = obj.optString("frame_hex").takeIf { it.isNotBlank() },
                explanation   = obj.optString("explanation", "(no explanation)"),
                rawJson       = cleaned
            )
        } catch (e: Exception) {
            onError("Failed to parse AI response: ${e.message}\n\nRaw: $raw")
            null
        }
    }
}
