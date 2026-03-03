package com.example.cydcompanion.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Error
import androidx.compose.material.icons.filled.HourglassEmpty
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Psychology
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Send
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.cydcompanion.ai.AiBackend
import com.example.cydcompanion.ai.AiEngineClient
import com.example.cydcompanion.ai.HandshakeExporter
import com.example.cydcompanion.ai.WpaVerifier
import com.example.cydcompanion.data.CydRepository
import com.example.cydcompanion.data.SettingsStore
import com.example.cydcompanion.data.VaultManager
import com.example.cydcompanion.domain.AiSuggestion
import com.example.cydcompanion.domain.CrackResult
import com.example.cydcompanion.domain.EapolFrameInfo
import com.example.cydcompanion.domain.GeneratedWordlist
import com.example.cydcompanion.domain.TargetProfile
import kotlinx.coroutines.Dispatchers
import androidx.compose.runtime.snapshotFlow
import kotlinx.coroutines.flow.debounce
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// ─────────────────────────────────────────────────────────────────────────────
// Neon accent colours (matches TokyoNeon theme)
// ─────────────────────────────────────────────────────────────────────────────
private val neonCyan    = Color(0xFF00FFFF)
private val neonMagenta = Color(0xFFFF00FF)
private val neonLime    = Color(0xFF00FF41)
private val neonAmber   = Color(0xFFFFB300)
private val neonRed     = Color(0xFFFF1744)
private val surfaceDark = Color(0xFF0D0D1A)
private val cardDark    = Color(0xFF12122A)

@Composable
@OptIn(kotlinx.coroutines.FlowPreview::class)
fun AiScreen(repo: CydRepository) {
    val scope   = rememberCoroutineScope()
    val context = LocalContext.current
    val settings = remember { SettingsStore(context) }
    val vault = remember { VaultManager(context) }

    val savedBackendType by settings.aiBackendType.collectAsState(initial = "Ollama")
    val savedOllamaUrl   by settings.ollamaUrl.collectAsState(initial = "http://localhost:11434")
    val savedOllamaModel by settings.ollamaModel.collectAsState(initial = "mistral")
    val savedOpenAiModel by settings.openAiModel.collectAsState(initial = "gpt-4o-mini")

    val vaultProjectId by settings.vaultProjectId.collectAsState(initial = "default")
    val vaultCaptureId by settings.vaultCaptureId.collectAsState(initial = null)
    val vaultCaptureLabel by settings.vaultCaptureLabel.collectAsState(initial = null)

    // ── UI state ──────────────────────────────────────────────────────────────
    var targetProfile  by remember { mutableStateOf<TargetProfile?>(null) }
    var aiSuggestion   by remember { mutableStateOf<AiSuggestion?>(null) }
    var statusMsg      by remember { mutableStateOf("") }
    var isLoadingProf  by remember { mutableStateOf(false) }
    var isLoadingAi    by remember { mutableStateOf(false) }
    var isExecuting    by remember { mutableStateOf(false) }
    var isRefining     by remember { mutableStateOf(false) }

    // ── AI Chat state ───────────────────────────────────────────────────────
    data class ChatLine(val role: String, val text: String)
    var chatInput by remember { mutableStateOf("") }
    var isChatting by remember { mutableStateOf(false) }
    var chatLines by remember {
        mutableStateOf(
            listOf(
                ChatLine("assistant", "AI chat ready. Ask a question (authorized testing / defensive).")
            )
        )
    }

    // ── CrackBot state ────────────────────────────────────────────────────────
    var generatedWordlist by remember { mutableStateOf<GeneratedWordlist?>(null) }
    var crackResult       by remember { mutableStateOf<CrackResult?>(null) }
    var crackProgress     by remember { mutableStateOf("") }
    var isGenerating      by remember { mutableStateOf(false) }
    var isCracking        by remember { mutableStateOf(false) }
    var exportedPath      by remember { mutableStateOf<String?>(null) }

    // ── Backend configuration ─────────────────────────────────────────────────
    var backendType    by remember { mutableStateOf("Ollama") }   // "Ollama" | "OpenAI"
    var ollamaUrl      by remember { mutableStateOf("") }
    var ollamaModel    by remember { mutableStateOf("") }
    var openAiKey      by remember { mutableStateOf("") }
    var openAiModel    by remember { mutableStateOf("") }
    var showApiKey     by remember { mutableStateOf(false) }
    var saveStatusMsg  by remember { mutableStateOf("") }

    var includeVaultContext by remember { mutableStateOf(true) }

    var didInitSettings by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) {
        settings.migrateAiDefaultsIfLegacy()
    }
    LaunchedEffect(savedBackendType, savedOllamaUrl, savedOllamaModel, savedOpenAiModel) {
        if (!didInitSettings) {
            backendType = savedBackendType
            ollamaUrl = savedOllamaUrl.ifBlank { "http://localhost:11434" }
            ollamaModel = savedOllamaModel.ifBlank { "mistral" }
            openAiModel = savedOpenAiModel.ifBlank { "gpt-4o-mini" }
            didInitSettings = true
        }
    }

    // Persist backend settings (debounced to avoid writing every keystroke)
    LaunchedEffect(Unit) {
        snapshotFlow { backendType }
            .distinctUntilChanged()
            .collect { settings.setAiBackendType(it) }
    }
    LaunchedEffect(Unit) {
        snapshotFlow { ollamaUrl }
            .debounce(400)
            .distinctUntilChanged()
            .filter { it.isNotBlank() }
            .collect { settings.setOllamaUrl(it) }
    }
    LaunchedEffect(Unit) {
        snapshotFlow { ollamaModel }
            .debounce(400)
            .distinctUntilChanged()
            .filter { it.isNotBlank() }
            .collect { settings.setOllamaModel(it) }
    }
    LaunchedEffect(Unit) {
        snapshotFlow { openAiModel }
            .debounce(400)
            .distinctUntilChanged()
            .filter { it.isNotBlank() }
            .collect { settings.setOpenAiModel(it) }
    }

    // ── Helpers ──────────────────────────────────────────────────────────────
    fun makeBackend(): AiBackend = if (backendType == "Ollama")
        AiBackend.Ollama(ollamaUrl.trim(), ollamaModel.trim())
    else
        AiBackend.OpenAI(openAiKey.trim(), openAiModel.trim())

    fun loadProfile() {
        isLoadingProf = true
        statusMsg = "Fetching EAPOL frames from CYD..."
        scope.launch {
            val p = repo.fetchAiFrames()
            targetProfile = p
            isLoadingProf = false
            statusMsg = if (p != null)
                "Target loaded: ${p.ssid} (${p.frames.size} EAPOL frames)"
            else
                "No active target on CYD, or YOINK not running."
        }
    }

    fun runAi() {
        val p = targetProfile ?: run {
            statusMsg = "Load a target first."; return
        }
        isLoadingAi = true
        aiSuggestion = null
        statusMsg = "Contacting AI backend (${makeBackend().javaClass.simpleName})…"
        scope.launch {
            val eng = AiEngineClient(makeBackend())
            val s = withContext(Dispatchers.IO) {
                eng.analyze(p) { err -> statusMsg = err }
            }
            aiSuggestion = s
            isLoadingAi = false
            if (s != null) statusMsg = "AI strategy ready: ${s.injectionType.uppercase()}"
        }
    }

    fun executeInjection() {
        val s = aiSuggestion ?: run { statusMsg = "Generate an AI suggestion first."; return }
        val p = targetProfile ?: run { statusMsg = "No target profile."; return }
        isExecuting = true
        statusMsg = "Sending injection to CYD…"
        scope.launch {
            val ok = when (s.injectionType.lowercase()) {
                "raw" -> s.frameHex?.let { hex ->
                    repo.injectRaw(channel = p.channel, frameHex = hex)
                } ?: false
                else  -> repo.injectDeauth(
                    bssid   = p.bssid,
                    client  = p.clientMac ?: "FF:FF:FF:FF:FF:FF",
                    reason  = s.reasonCode,
                    count   = s.deauthCount,
                    channel = p.channel
                )
            }
            isExecuting = false
            statusMsg = if (ok) "✓ Injection transmitted successfully" else "✗ Injection failed — check CYD connection"
        }
    }

    fun refineWithTelemetry() {
        val p = targetProfile ?: run { statusMsg = "Load a target first."; return }
        val prev = aiSuggestion ?: run { statusMsg = "Generate a strategy first."; return }
        isRefining = true
        statusMsg = "Reading CYD telemetry and refining strategy…"
        scope.launch {
            val telemetry = repo.fetchInjectionTelemetry()
            if (telemetry == null) {
                isRefining = false
                statusMsg = "✗ Could not read /api/status telemetry"
                return@launch
            }

            val eng = AiEngineClient(makeBackend())
            val refined = withContext(Dispatchers.IO) {
                eng.refineSuggestion(p, prev, telemetry) { err -> statusMsg = err }
            }
            isRefining = false
            if (refined != null) {
                aiSuggestion = refined
                statusMsg = "✓ Refined strategy ready: ${refined.injectionType.uppercase()}"
            } else {
                statusMsg = "✗ AI refinement failed"
            }
        }
    }

    fun generateWordlist() {
        val p = targetProfile ?: run { statusMsg = "Load a target first."; return }
        isGenerating = true
        generatedWordlist = null
        crackResult = null
        statusMsg = "Generating targeted wordlist (${makeBackend().javaClass.simpleName})…"
        scope.launch {
            val eng = AiEngineClient(makeBackend())
            val wl = withContext(Dispatchers.IO) {
                eng.generateWordlist(p) { err -> statusMsg = err }
            }
            generatedWordlist = wl
            isGenerating = false
            statusMsg = if (wl != null)
                "Wordlist ready: ${wl.candidates.size} candidates  (confidence ${"%.0f".format(wl.confidence * 100)}%)"
            else
                "✗ Wordlist generation failed"
        }
    }

    fun runCpuCrack() {
        val p  = targetProfile   ?: run { statusMsg = "Load a target first."; return }
        val wl = generatedWordlist ?: run { statusMsg = "Generate a wordlist first."; return }
        if (wl.candidates.isEmpty()) { statusMsg = "Wordlist is empty."; return }
        isCracking = true
        crackResult = null
        crackProgress = "Starting CPU crack — ${wl.candidates.size} candidates…"
        scope.launch {
            val result = WpaVerifier.crack(p, wl.candidates) { tried, total, pw ->
                crackProgress = "[$tried/$total] Trying: $pw"
            }
            crackResult = CrackResult(
                found            = result.found,
                password         = result.password,
                trialsCompleted  = result.trialsCompleted,
                elapsedMs        = result.elapsedMs,
                error            = result.error
            )
            isCracking = false
            statusMsg = when {
                result.error != null -> "✗ Crack error: ${result.error}"
                result.found -> "✓ PASSWORD FOUND: ${result.password}"
                else -> "✗ Not found in ${result.trialsCompleted} candidates (${result.elapsedMs}ms)"
            }
        }
    }

    fun exportHandshake() {
        val p = targetProfile ?: run { statusMsg = "No profile to export."; return }
        scope.launch {
            val result = withContext(Dispatchers.IO) {
                HandshakeExporter.saveHc22000(context, p)
            }
            exportedPath = result.filePath
            statusMsg = if (result.error != null)
                "✗ Export failed: ${result.error}"
            else
                "✓ Saved to Downloads: ${result.filePath?.takeLast(40)}"
        }
    }

    fun sendChat() {
        val text = chatInput.trim()
        if (text.isBlank()) return
        isChatting = true
        chatInput = ""
        chatLines = chatLines + ChatLine("user", text)

        scope.launch {
            val eng = AiEngineClient(makeBackend())
            val history = chatLines
                .takeLast(24)
                .map { AiEngineClient.ChatTurn(role = it.role, content = it.text) }

            val userMessage = if (includeVaultContext && !vaultCaptureId.isNullOrBlank()) {
                val summary = withContext(Dispatchers.IO) {
                    vault.buildVaultContextSummary(
                        projectId = vaultProjectId,
                        captureId = vaultCaptureId!!,
                        maxFiles = 200,
                        maxTextBytes = 24_000
                    )
                }
                summary + "\n\n=== USER MESSAGE ===\n" + text
            } else {
                text
            }

            val reply = withContext(Dispatchers.IO) {
                eng.chat(userMessage = userMessage, history = history) { err -> statusMsg = err }
            }
            isChatting = false
            chatLines = chatLines + ChatLine("assistant", reply ?: "(no response)")
        }
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(surfaceDark)
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 14.dp, vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        // Reserve top-right safe zone so the draggable hypercube never obscures
        // the AI/Deauth hunter header region and first actionable controls.
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = 116.dp)
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(end = 120.dp)
            ) {
                // Header
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Filled.Psychology, contentDescription = null, tint = neonMagenta, modifier = Modifier.size(22.dp))
                    Spacer(Modifier.width(8.dp))
                    Text("AI PENTEST ENGINE", color = neonMagenta, fontWeight = FontWeight.Black,
                        letterSpacing = 2.sp, fontSize = 15.sp)
                }

                // ── Status bar ────────────────────────────────────────────────
                if (statusMsg.isNotBlank()) {
                    val color = when {
                        statusMsg.startsWith("✓") -> neonLime
                        statusMsg.startsWith("✗") -> neonRed
                        else                      -> neonCyan
                    }
                    Spacer(Modifier.height(8.dp))
                    Text(
                        text = statusMsg,
                        color = color,
                        fontSize = 11.sp,
                        fontFamily = FontFamily.Monospace,
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(cardDark, RoundedCornerShape(4.dp))
                            .padding(8.dp)
                    )
                }
            }
        }

        // ── [1] Target Intel ──────────────────────────────────────────────────
        NeonCard(title = "TARGET INTEL", accentColor = neonCyan) {
            if (isLoadingProf) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(color = neonCyan, modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                    Spacer(Modifier.width(8.dp))
                    Text("Fetching…", color = neonCyan, fontSize = 12.sp)
                }
            } else if (targetProfile == null) {
                Text("No target loaded. Tap FETCH to pull EAPOL frames from the CYD.",
                    color = Color.Gray, fontSize = 12.sp)
            } else {
                TargetProfileView(targetProfile!!)
            }
            Spacer(Modifier.height(6.dp))
            Button(
                onClick = ::loadProfile,
                enabled = !isLoadingProf,
                colors = ButtonDefaults.buttonColors(containerColor = neonCyan.copy(alpha = 0.15f),
                    contentColor = neonCyan),
                border = ButtonDefaults.outlinedButtonBorder.copy(
                    brush = androidx.compose.ui.graphics.SolidColor(neonCyan.copy(alpha = 0.5f))
                ),
                modifier = Modifier.fillMaxWidth()
            ) {
                Icon(Icons.Filled.Refresh, contentDescription = null, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(6.dp))
                Text("FETCH FROM CYD", fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
            }
        }

        // ── [2] AI Configuration ──────────────────────────────────────────────
        NeonCard(title = "AI CONFIGURATION", accentColor = neonMagenta) {
            // Backend selector
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf("Ollama", "OpenAI").forEach { type ->
                    FilterChip(
                        selected = backendType == type,
                        onClick  = { backendType = type; aiSuggestion = null },
                        label    = { Text(type, fontSize = 12.sp) },
                        colors   = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = neonMagenta.copy(alpha = 0.2f),
                            selectedLabelColor     = neonMagenta
                        )
                    )
                }
            }
            Spacer(Modifier.height(4.dp))
            if (backendType == "Ollama") {
                NeonTextField(value = ollamaUrl, onValueChange = { ollamaUrl = it },
                    label = "Ollama URL (http://LAN-IP:11434)")
                NeonTextField(value = ollamaModel, onValueChange = { ollamaModel = it },
                    label = "Model  (llama3.2, mistral, etc.)")
                Spacer(Modifier.height(8.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Button(
                        onClick = {
                            scope.launch {
                                settings.setOllamaUrl(ollamaUrl)
                                settings.setOllamaModel(ollamaModel)
                                saveStatusMsg = "✓ Saved"
                                kotlinx.coroutines.delay(2000)
                                saveStatusMsg = ""
                            }
                        },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = neonMagenta.copy(alpha = 0.15f),
                            contentColor = neonMagenta
                        ),
                        modifier = Modifier.weight(1f)
                    ) {
                        Text("SAVE", fontWeight = FontWeight.Bold, fontSize = 12.sp)
                    }
                    OutlinedButton(
                        onClick = {
                            ollamaUrl = "http://localhost:11434"
                            ollamaModel = "mistral"
                            scope.launch {
                                settings.setOllamaUrl(ollamaUrl)
                                settings.setOllamaModel(ollamaModel)
                                saveStatusMsg = "✓ Reset to defaults"
                                kotlinx.coroutines.delay(2000)
                                saveStatusMsg = ""
                            }
                        },
                        colors = ButtonDefaults.outlinedButtonColors(
                            contentColor = neonMagenta.copy(alpha = 0.7f)
                        ),
                        modifier = Modifier.weight(1f)
                    ) {
                        Text("RESET", fontWeight = FontWeight.Bold, fontSize = 12.sp)
                    }
                }
                if (saveStatusMsg.isNotEmpty()) {
                    Text(
                        text = saveStatusMsg,
                        color = neonLime,
                        fontSize = 11.sp,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                }
            } else {
                NeonTextField(
                    value = openAiKey,
                    onValueChange = { openAiKey = it },
                    label = "OpenAI API Key (sk-…)",
                    isPassword = !showApiKey
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Checkbox(checked = showApiKey, onCheckedChange = { showApiKey = it },
                        colors = CheckboxDefaults.colors(checkedColor = neonMagenta))
                    Text("Show key", color = Color.LightGray, fontSize = 11.sp)
                }
                NeonTextField(value = openAiModel, onValueChange = { openAiModel = it },
                    label = "Model  (gpt-4o-mini, gpt-4o)")
                Spacer(Modifier.height(8.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Button(
                        onClick = {
                            scope.launch {
                                settings.setOpenAiModel(openAiModel)
                                saveStatusMsg = "✓ Saved"
                                kotlinx.coroutines.delay(2000)
                                saveStatusMsg = ""
                            }
                        },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = neonMagenta.copy(alpha = 0.15f),
                            contentColor = neonMagenta
                        ),
                        modifier = Modifier.weight(1f)
                    ) {
                        Text("SAVE", fontWeight = FontWeight.Bold, fontSize = 12.sp)
                    }
                    if (saveStatusMsg.isNotEmpty()) {
                        Text(
                            text = saveStatusMsg,
                            color = neonLime,
                            fontSize = 11.sp,
                            modifier = Modifier.align(Alignment.CenterVertically)
                        )
                    }
                }
            }
        }

        // ── [3] AI Suggestion ─────────────────────────────────────────────────
        NeonCard(title = "AI SUGGESTION", accentColor = neonLime) {
            when {
                isLoadingAi -> Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(color = neonLime, modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                    Spacer(Modifier.width(8.dp))
                    Text("Waiting for LLM…", color = neonLime, fontSize = 12.sp)
                }
                aiSuggestion != null -> AiSuggestionView(aiSuggestion!!)
                else -> Text("Run analysis to see vendor-specific injection parameters.",
                    color = Color.Gray, fontSize = 12.sp)
            }
            Spacer(Modifier.height(6.dp))
            Button(
                onClick = ::runAi,
                enabled = !isLoadingAi && targetProfile != null,
                colors = ButtonDefaults.buttonColors(containerColor = neonLime.copy(alpha = 0.15f),
                    contentColor = neonLime),
                modifier = Modifier.fillMaxWidth()
            ) {
                Icon(Icons.Filled.Psychology, contentDescription = null, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(6.dp))
                Text("ANALYSE WITH AI", fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
            }
        }

        // ── [3b] AI Chat ─────────────────────────────────────────────────────
        NeonCard(title = "AI CHAT", accentColor = neonMagenta) {
            Text(
                "Quick chat (uses the same backend config).",
                color = Color.Gray,
                fontSize = 11.sp
            )
            Spacer(Modifier.height(6.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column {
                    Text(
                        text = "Use Vault Context",
                        color = Color.LightGray,
                        fontSize = 11.sp
                    )
                    Text(
                        text = "Active: ${vaultCaptureLabel ?: "(none)"}",
                        color = Color.Gray,
                        fontSize = 10.sp
                    )
                }
                Switch(
                    checked = includeVaultContext,
                    onCheckedChange = { includeVaultContext = it },
                    enabled = !vaultCaptureId.isNullOrBlank()
                )
            }
            Spacer(Modifier.height(6.dp))
            Button(
                onClick = {
                    scope.launch {
                        includeVaultContext = true
                        val summary = vault.buildVaultContextSummary(
                            vaultProjectId,
                            vaultCaptureId ?: "default"
                        )
                        if (summary.isNotEmpty()) {
                            chatLines = chatLines + ChatLine(
                                "assistant",
                                "✓ Synced ${summary.lines().size} lines of vault context. Ready for pentest analysis."
                            )
                        } else {
                            chatLines = chatLines + ChatLine(
                                "assistant",
                                "⚠ No vault data found for active capture."
                            )
                        }
                    }
                },
                enabled = !vaultCaptureId.isNullOrBlank() && !isChatting,
                colors = ButtonDefaults.buttonColors(
                    containerColor = neonCyan.copy(alpha = 0.15f),
                    contentColor = neonCyan
                ),
                modifier = Modifier.fillMaxWidth()
            ) {
                Icon(Icons.Filled.Refresh, contentDescription = null, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(6.dp))
                Text("SYNC CAPTURES TO AI", fontWeight = FontWeight.Bold, fontSize = 12.sp, letterSpacing = 1.sp)
            }
            Spacer(Modifier.height(6.dp))

            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(180.dp)
                    .background(Color(0xFF0A0A12), RoundedCornerShape(6.dp))
                    .border(1.dp, neonMagenta.copy(alpha = 0.25f), RoundedCornerShape(6.dp))
                    .padding(8.dp)
            ) {
                LazyColumn(
                    modifier = Modifier.fillMaxSize(),
                    reverseLayout = true
                ) {
                    items(chatLines.asReversed()) { line ->
                        val tint = if (line.role == "user") neonCyan else Color.LightGray
                        Text(
                            text = (if (line.role == "user") "YOU: " else "AI: ") + line.text,
                            color = tint,
                            fontSize = 10.sp,
                            fontFamily = FontFamily.Monospace,
                            modifier = Modifier.padding(bottom = 6.dp)
                        )
                    }
                }
            }

            Spacer(Modifier.height(8.dp))

            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(
                    value = chatInput,
                    onValueChange = { chatInput = it },
                    label = { Text("Ask a question", fontSize = 11.sp) },
                    singleLine = true,
                    textStyle = LocalTextStyle.current.copy(
                        fontSize = 12.sp,
                        fontFamily = FontFamily.Monospace
                    ),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = neonMagenta,
                        unfocusedBorderColor = Color.DarkGray,
                        focusedLabelColor = neonMagenta,
                        cursorColor = neonMagenta,
                        focusedTextColor = Color.White,
                        unfocusedTextColor = Color.LightGray
                    ),
                    modifier = Modifier.weight(1f)
                )
                Spacer(Modifier.width(8.dp))
                Button(
                    onClick = ::sendChat,
                    enabled = !isChatting && chatInput.isNotBlank(),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = neonMagenta.copy(alpha = 0.18f),
                        contentColor = neonMagenta
                    )
                ) {
                    if (isChatting) {
                        CircularProgressIndicator(
                            color = neonMagenta,
                            modifier = Modifier.size(16.dp),
                            strokeWidth = 2.dp
                        )
                    } else {
                        Icon(Icons.Filled.Send, contentDescription = null, modifier = Modifier.size(16.dp))
                    }
                }
            }
        }

        // ── [4] Execute ───────────────────────────────────────────────────────
        NeonCard(title = "EXECUTE ON CYD", accentColor = neonRed) {
            if (aiSuggestion == null) {
                Text("Generate an AI suggestion first.", color = Color.Gray, fontSize = 12.sp)
            } else {
                val s = aiSuggestion!!
                Text("Will transmit: ${s.injectionType.uppercase()}  •  reason ${s.reasonCode}  •  ×${s.deauthCount}",
                    color = neonAmber, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
                Spacer(Modifier.height(2.dp))
                Text("Target: ${targetProfile?.bssid ?: "?"}  ch${targetProfile?.channel ?: "?"}",
                    color = Color.LightGray, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
            }
            Spacer(Modifier.height(6.dp))
            Button(
                onClick = ::executeInjection,
                enabled = !isExecuting && aiSuggestion != null && targetProfile != null,
                colors = ButtonDefaults.buttonColors(containerColor = neonRed.copy(alpha = 0.2f),
                    contentColor = neonRed),
                modifier = Modifier.fillMaxWidth()
            ) {
                if (isExecuting)
                    CircularProgressIndicator(color = neonRed, modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                else
                    Icon(Icons.Filled.Send, contentDescription = null, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(6.dp))
                Text(if (isExecuting) "TRANSMITTING…" else "INJECT",
                    fontWeight = FontWeight.Black, letterSpacing = 2.sp)
            }

            Spacer(Modifier.height(6.dp))
            OutlinedButton(
                onClick = ::refineWithTelemetry,
                enabled = !isRefining && aiSuggestion != null && targetProfile != null,
                colors = ButtonDefaults.outlinedButtonColors(contentColor = neonAmber),
                border = ButtonDefaults.outlinedButtonBorder.copy(
                    brush = androidx.compose.ui.graphics.SolidColor(neonAmber.copy(alpha = 0.45f))
                ),
                modifier = Modifier.fillMaxWidth()
            ) {
                if (isRefining) {
                    CircularProgressIndicator(
                        color = neonAmber,
                        modifier = Modifier.size(16.dp),
                        strokeWidth = 2.dp
                    )
                } else {
                    Icon(Icons.Filled.Refresh, contentDescription = null, modifier = Modifier.size(16.dp))
                }
                Spacer(Modifier.width(6.dp))
                Text(if (isRefining) "REFINING…" else "REFINE FROM TELEMETRY",
                    fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
            }
        }

        // ── [5] CrackBot Pipeline ─────────────────────────────────────────────
        NeonCard(title = "CRACKBOT — AI WORDLIST + CPU CRACK", accentColor = neonAmber) {

            Text(
                "CrackBot-style: AI generates targeted password candidates from network metadata " +
                "(vendor defaults, ISP schemes, SSID patterns). Android CPU verifies via " +
                "PBKDF2+PTK+MIC. Export .22000 for hashcat offline.",
                color = Color.Gray, fontSize = 10.sp, lineHeight = 14.sp
            )
            Spacer(Modifier.height(8.dp))

            // ── Wordlist generation ─────────────────────────────────────────
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("WORDLIST", color = neonAmber, fontSize = 10.sp,
                    fontWeight = FontWeight.Black, letterSpacing = 2.sp)
                if (generatedWordlist != null) {
                    Text("${generatedWordlist!!.candidates.size} candidates",
                        color = neonLime, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                }
            }
            Spacer(Modifier.height(4.dp))

            val wordlistSnap = generatedWordlist
            when {
                isGenerating -> Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(color = neonAmber, modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                    Spacer(Modifier.width(8.dp))
                    Text("Generating…", color = neonAmber, fontSize = 12.sp)
                }
                wordlistSnap != null -> {
                    val wl = wordlistSnap
                    Text("Strategy: ${wl.strategy}",
                        color = Color.LightGray, fontSize = 11.sp, lineHeight = 15.sp)
                    Spacer(Modifier.height(3.dp))
                    Text("Overall confidence: ${"%.0f".format(wl.confidence * 100)}%",
                        color = neonAmber, fontSize = 11.sp)
                    Spacer(Modifier.height(4.dp))
                    // Top 5 candidates preview
                    wl.candidates.take(5).forEach { c ->
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                            Text(c.password, color = Color.White, fontSize = 11.sp,
                                fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                            Text("${"%.0f".format(c.confidence * 100)}%",
                                color = neonLime, fontSize = 10.sp)
                        }
                        Text(c.rationale, color = Color.Gray, fontSize = 9.sp,
                            modifier = Modifier.padding(start = 12.dp, bottom = 3.dp))
                    }
                    if (wl.candidates.size > 5)
                        Text("  … and ${wl.candidates.size - 5} more",
                            color = Color.DarkGray, fontSize = 10.sp)
                }
                else -> Text("Tap GENERATE to ask AI for targeted password guesses.",
                    color = Color.Gray, fontSize = 12.sp)
            }
            Spacer(Modifier.height(6.dp))
            Button(
                onClick = ::generateWordlist,
                enabled = !isGenerating && targetProfile != null,
                colors = ButtonDefaults.buttonColors(
                    containerColor = neonAmber.copy(alpha = 0.15f),
                    contentColor   = neonAmber),
                modifier = Modifier.fillMaxWidth()
            ) {
                Icon(Icons.Filled.Lock, contentDescription = null, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(6.dp))
                Text("GENERATE WORDLIST", fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
            }

            // ── CPU MIC Verification ────────────────────────────────────────
            if (generatedWordlist != null) {
                Spacer(Modifier.height(10.dp))
                HorizontalDivider(color = neonAmber.copy(alpha = 0.2f), thickness = 0.5.dp)
                Spacer(Modifier.height(10.dp))

                Text("CPU CRACK", color = neonAmber, fontSize = 10.sp,
                    fontWeight = FontWeight.Black, letterSpacing = 2.sp)
                Spacer(Modifier.height(4.dp))

                when {
                    isCracking -> Column {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            CircularProgressIndicator(color = neonAmber,
                                modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                            Spacer(Modifier.width(8.dp))
                            Text("Cracking (PBKDF2+MIC)…", color = neonAmber, fontSize = 12.sp)
                        }
                        Spacer(Modifier.height(3.dp))
                        Text(crackProgress, color = Color.Gray, fontSize = 10.sp,
                            fontFamily = FontFamily.Monospace)
                    }
                    crackResult != null -> {
                        val cr = crackResult!!
                        if (cr.error != null) {
                            Text("Error: ${cr.error}", color = neonRed, fontSize = 11.sp)
                        } else if (cr.found) {
                            Text("PASSWORD FOUND", color = neonLime, fontSize = 13.sp,
                                fontWeight = FontWeight.Black, letterSpacing = 2.sp)
                            Spacer(Modifier.height(4.dp))
                            Text(cr.password ?: "", color = Color.White, fontSize = 18.sp,
                                fontWeight = FontWeight.Black, fontFamily = FontFamily.Monospace)
                            Spacer(Modifier.height(4.dp))
                            Text("Verified in ${cr.trialsCompleted} trials  •  ${cr.elapsedMs}ms",
                                color = Color.Gray, fontSize = 10.sp)
                        } else {
                            Text("Not in wordlist — ${cr.trialsCompleted} trials in ${cr.elapsedMs}ms",
                                color = neonAmber, fontSize = 11.sp)
                            Text("Try a larger candidate count or export for hashcat.",
                                color = Color.Gray, fontSize = 10.sp)
                        }
                    }
                    else -> Text(
                        "Verify candidates against the captured EAPOL MIC (needs M1+M2).",
                        color = Color.Gray, fontSize = 12.sp)
                }
                Spacer(Modifier.height(6.dp))
                Button(
                    onClick = ::runCpuCrack,
                    enabled = !isCracking && generatedWordlist != null
                            && targetProfile?.hasValidPair == true,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = neonAmber.copy(alpha = 0.15f),
                        contentColor   = neonAmber),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    if (isCracking)
                        CircularProgressIndicator(color = neonAmber,
                            modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                    else
                        Icon(Icons.Filled.HourglassEmpty, contentDescription = null,
                            modifier = Modifier.size(16.dp))
                    Spacer(Modifier.width(6.dp))
                    Text("VERIFY (CPU)", fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                }

                // ── Export ──────────────────────────────────────────────────
                Spacer(Modifier.height(6.dp))
                OutlinedButton(
                    onClick = ::exportHandshake,
                    enabled = targetProfile != null,
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = neonCyan),
                    border = ButtonDefaults.outlinedButtonBorder.copy(
                        brush = androidx.compose.ui.graphics.SolidColor(neonCyan.copy(alpha = 0.4f))),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Filled.Share, contentDescription = null, modifier = Modifier.size(16.dp))
                    Spacer(Modifier.width(6.dp))
                    Text("EXPORT .22000 FOR HASHCAT", fontSize = 11.sp,
                        fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                }
                if (exportedPath != null) {
                    Spacer(Modifier.height(3.dp))
                    Text("Saved: ${exportedPath!!.takeLast(50)}",
                        color = neonLime, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Sub-composables
// ─────────────────────────────────────────────────────────────────────────────

@Composable
private fun NeonCard(
    title: String,
    accentColor: Color,
    content: @Composable ColumnScope.() -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(cardDark, RoundedCornerShape(8.dp))
            .border(1.dp, accentColor.copy(alpha = 0.35f), RoundedCornerShape(8.dp))
            .padding(12.dp)
    ) {
        Text(title, color = accentColor, fontSize = 10.sp, fontWeight = FontWeight.Black,
            letterSpacing = 2.sp)
        Spacer(Modifier.height(8.dp))
        content()
    }
}

@Composable
private fun NeonTextField(
    value: String,
    onValueChange: (String) -> Unit,
    label: String,
    isPassword: Boolean = false
) {
    OutlinedTextField(
        value = value,
        onValueChange = onValueChange,
        label = { Text(label, fontSize = 11.sp) },
        singleLine = true,
        visualTransformation = if (isPassword) PasswordVisualTransformation() else VisualTransformation.None,
        keyboardOptions = KeyboardOptions(keyboardType = if (isPassword) KeyboardType.Password else KeyboardType.Uri),
        textStyle = LocalTextStyle.current.copy(fontSize = 12.sp, fontFamily = FontFamily.Monospace),
        colors = OutlinedTextFieldDefaults.colors(
            focusedBorderColor   = neonCyan,
            unfocusedBorderColor = Color.DarkGray,
            focusedLabelColor    = neonCyan,
            cursorColor          = neonCyan,
            focusedTextColor     = Color.White,
            unfocusedTextColor   = Color.LightGray
        ),
        modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp)
    )
}

@Composable
private fun TargetProfileView(p: TargetProfile) {
    Column(verticalArrangement = Arrangement.spacedBy(3.dp)) {
        ProfileRow("SSID",    p.ssid,     neonCyan)
        ProfileRow("BSSID",   p.bssid,    neonCyan)
        ProfileRow("AP",      "${p.apVendor}  (${p.apOui})", Color.LightGray)
        if (!p.clientMac.isNullOrBlank())
            ProfileRow("Client", "${p.staVendor ?: "Unknown"}  (${p.staOui ?: p.clientMac})", Color.LightGray)
        ProfileRow("Auth",    p.auth,     neonAmber)
        ProfileRow("Channel", "ch${p.channel}  RSSI ${p.rssi} dBm", Color.LightGray)
        ProfileRow("EAPOL",   "mask=0x${"%02X".format(p.eapolMask)}  " +
                (1..4).filter { p.eapolMask and (1 shl (it - 1)) != 0 }
                    .joinToString("+") { "M$it" } +
                if (p.hasValidPair) "  ✓ valid pair" else "  ✗ incomplete",
            if (p.hasValidPair) neonLime else neonAmber)
        Spacer(Modifier.height(4.dp))
        // Per-frame summary
        p.frames.forEach { f -> EapolFrameRow(f) }
    }
}

@Composable
private fun ProfileRow(label: String, value: String, valueColor: Color) {
    Row {
        Text("%-8s".format(label), color = Color.Gray, fontSize = 11.sp,
            fontFamily = FontFamily.Monospace)
        Text(value, color = valueColor, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
    }
}

@Composable
private fun EapolFrameRow(f: EapolFrameInfo) {
    Text(
        "  M${f.messageNum}: cipher=${f.cipherHint}  " +
                "MIC=${if (f.hasMic) "✓" else "✗"}  " +
                "ACK=${if (f.hasAck) "✓" else "✗"}  " +
                "Secure=${if (f.isSecure) "✓" else "✗"}  " +
                "rssi=${f.rssi}dBm",
        color = Color(0xFFAAAAAA),
        fontSize = 10.sp,
        fontFamily = FontFamily.Monospace
    )
}

@Composable
private fun AiSuggestionView(s: AiSuggestion) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Chip("TYPE",   s.injectionType.uppercase(), neonMagenta)
            Chip("REASON", "0x${"%02X".format(s.reasonCode)} (${s.reasonCode})", neonAmber)
            Chip("COUNT",  "×${s.deauthCount}", neonCyan)
            Chip("GAP",    "${s.intervalMs}ms", neonCyan)
        }
        Divider(color = Color.DarkGray, thickness = 0.5.dp)
        Text(s.explanation, color = Color.LightGray, fontSize = 11.sp, lineHeight = 16.sp)
        if (!s.frameHex.isNullOrBlank()) {
            Spacer(Modifier.height(2.dp))
            Text("FRAME HEX:", color = neonMagenta, fontSize = 10.sp, fontWeight = FontWeight.Bold)
            Text(s.frameHex.chunked(32).joinToString("\n"),
                color = Color(0xFF888888), fontSize = 9.sp, fontFamily = FontFamily.Monospace)
        }
    }
}

@Composable
private fun Chip(label: String, value: String, color: Color) {
    Column(
        modifier = Modifier
            .background(color.copy(alpha = 0.12f), RoundedCornerShape(4.dp))
            .padding(horizontal = 6.dp, vertical = 3.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(label, color = Color.Gray, fontSize = 8.sp, letterSpacing = 1.sp)
        Text(value, color = color, fontSize = 11.sp, fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.Monospace)
    }
}
