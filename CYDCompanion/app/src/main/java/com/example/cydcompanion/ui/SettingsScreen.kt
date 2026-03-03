package com.example.cydcompanion.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.cydcompanion.data.CydRepository
import com.example.cydcompanion.data.RootAccessManager
import com.example.cydcompanion.data.SettingsStore
import com.example.cydcompanion.domain.OperationMode
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@Composable
fun SettingsScreen(repo: CydRepository) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val settings = remember(context) { SettingsStore(context) }
    val connection by repo.connection.collectAsState()

    val cydIp by settings.cydIp.collectAsState(initial = "192.168.4.1")
    val operationMode by settings.operationMode.collectAsState(initial = OperationMode.COMPANION.name)
    val launchTab by settings.launchTab.collectAsState(initial = "live")
    val autoReconnect by settings.autoReconnect.collectAsState(initial = true)
    val keepScreenAwake by settings.keepScreenAwake.collectAsState(initial = false)
    val showLegalDisclaimer by settings.showLegalDisclaimer.collectAsState(initial = true)
    val hapticFeedback by settings.hapticFeedback.collectAsState(initial = true)
    val enableAnimations by settings.enableAnimations.collectAsState(initial = true)
    val showHypercube by settings.showHypercube.collectAsState(initial = true)
    val compactUi by settings.compactUi.collectAsState(initial = false)

    val preferUsb by settings.preferUsb.collectAsState(initial = false)
    val packetPollMs by settings.packetPollMs.collectAsState(initial = 260)
    val consolePollMs by settings.consolePollMs.collectAsState(initial = 450)
    val packetBufferMax by settings.packetBufferMax.collectAsState(initial = 500)
    val consoleBufferMax by settings.consoleBufferMax.collectAsState(initial = 200)
    val autoSyncBadge by settings.autoSyncBadge.collectAsState(initial = true)

    val aiAutoRefine by settings.aiAutoRefine.collectAsState(initial = true)
    val autoPullAllowedOnly by settings.autoPullAllowedOnly.collectAsState(initial = true)
    val maxImportMb by settings.maxImportMb.collectAsState(initial = 256)
    val exportIncludeMetadata by settings.exportIncludeMetadata.collectAsState(initial = true)
    val maskMacAddresses by settings.maskMacAddresses.collectAsState(initial = false)

    var cydIpEdit by remember(cydIp) { mutableStateOf(cydIp) }
    var packetPollEdit by remember(packetPollMs) { mutableStateOf(packetPollMs.toString()) }
    var consolePollEdit by remember(consolePollMs) { mutableStateOf(consolePollMs.toString()) }
    var packetBufferEdit by remember(packetBufferMax) { mutableStateOf(packetBufferMax.toString()) }
    var consoleBufferEdit by remember(consoleBufferMax) { mutableStateOf(consoleBufferMax.toString()) }
    var maxImportEdit by remember(maxImportMb) { mutableStateOf(maxImportMb.toString()) }

    var rootStatus by remember { mutableStateOf(RootAccessManager.quickDetect().message) }
    var rootBusy by remember { mutableStateOf(false) }
    var saveStatus by remember { mutableStateOf("") }

    val launchTabs = listOf("live", "data", "ops", "control", "export", "cli", "ai")
    val modeOptions = listOf(OperationMode.COMPANION.name, OperationMode.STANDALONE.name)

    ScreenScaffold(title = "Settings") {
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            item {
                NeonCard(header = "General") {
                    Text(
                        text = "Connection: ${connection.status.name}" +
                            (connection.deviceName?.let { " (${it})" } ?: ""),
                        style = MaterialTheme.typography.bodySmall.copy(color = Color.LightGray)
                    )
                    Spacer(Modifier.height(8.dp))
                    OutlinedTextField(
                        value = cydIpEdit,
                        onValueChange = { cydIpEdit = it },
                        label = { Text("Default CYD IP") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth()
                    )
                    Spacer(Modifier.height(8.dp))
                    NeonButton(
                        text = "Save IP",
                        onClick = {
                            scope.launch {
                                settings.setCydIp(cydIpEdit)
                                saveStatus = "Saved default IP."
                            }
                        },
                        modifier = Modifier.fillMaxWidth()
                    )

                    Spacer(Modifier.height(10.dp))
                    Text("Default Mode", style = MaterialTheme.typography.labelMedium.copy(color = Color.LightGray))
                    Spacer(Modifier.height(6.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                        modeOptions.forEach { mode ->
                            NeonButton(
                                text = mode,
                                onClick = { scope.launch { settings.setOperationMode(mode) } },
                                enabled = operationMode != mode,
                                modifier = Modifier.weight(1f)
                            )
                        }
                    }

                    Spacer(Modifier.height(10.dp))
                    Text("Launch Tab", style = MaterialTheme.typography.labelMedium.copy(color = Color.LightGray))
                    Spacer(Modifier.height(6.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp), modifier = Modifier.fillMaxWidth()) {
                        launchTabs.take(4).forEach { tab ->
                            NeonButton(
                                text = tab,
                                onClick = { scope.launch { settings.setLaunchTab(tab) } },
                                enabled = launchTab != tab,
                                modifier = Modifier.weight(1f)
                            )
                        }
                    }
                    Spacer(Modifier.height(6.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp), modifier = Modifier.fillMaxWidth()) {
                        launchTabs.drop(4).forEach { tab ->
                            NeonButton(
                                text = tab,
                                onClick = { scope.launch { settings.setLaunchTab(tab) } },
                                enabled = launchTab != tab,
                                modifier = Modifier.weight(1f)
                            )
                        }
                    }
                }
            }

            item {
                NeonCard(header = "UI & Behavior") {
                    SettingSwitchRow("Auto Reconnect", "Reconnect transport on app reopen", autoReconnect) {
                        scope.launch { settings.setAutoReconnect(it) }
                    }
                    SettingSwitchRow("Keep Screen Awake", "Prevent sleep during active sessions", keepScreenAwake) {
                        scope.launch { settings.setKeepScreenAwake(it) }
                    }
                    SettingSwitchRow("Haptic Feedback", "Vibration on key actions", hapticFeedback) {
                        scope.launch { settings.setHapticFeedback(it) }
                    }
                    SettingSwitchRow("Enable Animations", "Backdrop and motion effects", enableAnimations) {
                        scope.launch { settings.setEnableAnimations(it) }
                    }
                    SettingSwitchRow("Show Hypercube", "Display floating hypercube overlay", showHypercube) {
                        scope.launch { settings.setShowHypercube(it) }
                    }
                    SettingSwitchRow("Compact UI", "Denser layout for smaller phones", compactUi) {
                        scope.launch { settings.setCompactUi(it) }
                    }
                }
            }

            item {
                NeonCard(header = "Transport & Polling") {
                    SettingSwitchRow("Prefer USB", "Choose USB first when both links exist", preferUsb) {
                        scope.launch { settings.setPreferUsb(it) }
                    }
                    SettingSwitchRow("Auto Sync Badge", "Toggle CYD companion sync indicator", autoSyncBadge) {
                        scope.launch { settings.setAutoSyncBadge(it) }
                    }

                    Spacer(Modifier.height(8.dp))
                    NumberSettingRow("Packet Poll (ms)", packetPollEdit, onValueChange = { packetPollEdit = it }) {
                        packetPollEdit.toIntOrNull()?.let { v ->
                            scope.launch {
                                settings.setPacketPollMs(v)
                                saveStatus = "Saved packet poll interval."
                            }
                        }
                    }
                    NumberSettingRow("Console Poll (ms)", consolePollEdit, onValueChange = { consolePollEdit = it }) {
                        consolePollEdit.toIntOrNull()?.let { v ->
                            scope.launch {
                                settings.setConsolePollMs(v)
                                saveStatus = "Saved console poll interval."
                            }
                        }
                    }
                }
            }

            item {
                NeonCard(header = "Capture & Data") {
                    SettingSwitchRow("Pull Allowed Types Only", "Restrict pulls to vetted capture extensions", autoPullAllowedOnly) {
                        scope.launch { settings.setAutoPullAllowedOnly(it) }
                    }
                    SettingSwitchRow("Export Metadata", "Attach session metadata during exports", exportIncludeMetadata) {
                        scope.launch { settings.setExportIncludeMetadata(it) }
                    }

                    Spacer(Modifier.height(8.dp))
                    NumberSettingRow("Packet Buffer Max", packetBufferEdit, onValueChange = { packetBufferEdit = it }) {
                        packetBufferEdit.toIntOrNull()?.let { v ->
                            scope.launch {
                                settings.setPacketBufferMax(v)
                                saveStatus = "Saved packet buffer limit."
                            }
                        }
                    }
                    NumberSettingRow("Console Buffer Max", consoleBufferEdit, onValueChange = { consoleBufferEdit = it }) {
                        consoleBufferEdit.toIntOrNull()?.let { v ->
                            scope.launch {
                                settings.setConsoleBufferMax(v)
                                saveStatus = "Saved console buffer limit."
                            }
                        }
                    }
                    NumberSettingRow("Max Import (MB)", maxImportEdit, onValueChange = { maxImportEdit = it }) {
                        maxImportEdit.toIntOrNull()?.let { v ->
                            scope.launch {
                                settings.setMaxImportMb(v)
                                saveStatus = "Saved import limit."
                            }
                        }
                    }
                }
            }

            item {
                NeonCard(header = "AI & Security") {
                    SettingSwitchRow("AI Auto-Refine", "Refine suggestions automatically with telemetry", aiAutoRefine) {
                        scope.launch { settings.setAiAutoRefine(it) }
                    }
                    SettingSwitchRow("Show Legal Disclaimer", "Display authorized-use reminder", showLegalDisclaimer) {
                        scope.launch { settings.setShowLegalDisclaimer(it) }
                    }
                    SettingSwitchRow("Mask MAC Addresses", "Obfuscate MAC/BSSID in UI and logs", maskMacAddresses) {
                        scope.launch { settings.setMaskMacAddresses(it) }
                    }
                }
            }

            item {
                NeonCard(header = "Root Access") {
                    Text(
                        text = "Status: $rootStatus",
                        style = MaterialTheme.typography.bodySmall.copy(color = Color.LightGray)
                    )
                    Spacer(Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                        NeonButton(
                            text = if (rootBusy) "Checking..." else "Detect Root",
                            onClick = {
                                if (rootBusy) return@NeonButton
                                rootStatus = RootAccessManager.quickDetect().message
                            },
                            enabled = !rootBusy,
                            modifier = Modifier.weight(1f)
                        )
                        NeonButton(
                            text = if (rootBusy) "Please Wait" else "1-Click Root Access",
                            onClick = {
                                if (rootBusy) return@NeonButton
                                rootBusy = true
                                scope.launch {
                                    val result = withContext(Dispatchers.IO) { RootAccessManager.oneClickRootAccess() }
                                    rootStatus = buildString {
                                        append(result.message)
                                        if (!result.uidLine.isNullOrBlank()) append(" (${result.uidLine})")
                                    }
                                    rootBusy = false
                                }
                            },
                            enabled = !rootBusy,
                            modifier = Modifier.weight(1f)
                        )
                    }
                    Spacer(Modifier.height(8.dp))
                    NeonButton(
                        text = "Open Magisk",
                        onClick = {
                            val launch = context.packageManager.getLaunchIntentForPackage("com.topjohnwu.magisk")
                            if (launch != null) {
                                context.startActivity(launch)
                            } else {
                                rootStatus = "Magisk is not installed."
                            }
                        },
                        modifier = Modifier.fillMaxWidth()
                    )
                    Spacer(Modifier.height(6.dp))
                    Text(
                        text = "This requests root shell permission if your device is already rooted.",
                        style = MaterialTheme.typography.bodySmall.copy(color = Color.Gray, fontSize = 11.sp)
                    )
                }
            }

            if (saveStatus.isNotBlank()) {
                item {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(MaterialTheme.colorScheme.secondary.copy(alpha = 0.12f))
                            .border(1.dp, MaterialTheme.colorScheme.secondary.copy(alpha = 0.4f))
                            .padding(10.dp)
                    ) {
                        Text(
                            text = saveStatus,
                            style = MaterialTheme.typography.bodySmall.copy(
                                color = MaterialTheme.colorScheme.secondary,
                                fontWeight = FontWeight.SemiBold
                            )
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun SettingSwitchRow(
    title: String,
    subtitle: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyMedium.copy(color = Color.White, fontWeight = FontWeight.Bold))
            Text(subtitle, style = MaterialTheme.typography.bodySmall.copy(color = Color.Gray, fontSize = 11.sp))
        }
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange,
            colors = SwitchDefaults.colors(
                checkedThumbColor = MaterialTheme.colorScheme.secondary,
                checkedTrackColor = MaterialTheme.colorScheme.secondary.copy(alpha = 0.4f)
            )
        )
    }
}

@Composable
private fun NumberSettingRow(
    label: String,
    value: String,
    onValueChange: (String) -> Unit,
    onSave: () -> Unit
) {
    Text(label, style = MaterialTheme.typography.labelMedium.copy(color = Color.LightGray))
    Spacer(Modifier.height(4.dp))
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
        OutlinedTextField(
            value = value,
            onValueChange = { input -> onValueChange(input.filter { it.isDigit() }) },
            singleLine = true,
            modifier = Modifier.weight(1f)
        )
        NeonButton(
            text = "Save",
            onClick = onSave,
            modifier = Modifier.align(Alignment.CenterVertically)
        )
    }
    Spacer(Modifier.height(8.dp))
}
