package com.example.cydcompanion.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.CutCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateMapOf
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
import com.example.cydcompanion.data.CapabilityDetector
import com.example.cydcompanion.data.CydRepository
import com.example.cydcompanion.data.SettingsStore
import com.example.cydcompanion.domain.OperationMode
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import kotlin.math.roundToInt

@Composable
fun OpsScreen(repo: CydRepository) {
    val context = LocalContext.current
    val settings = remember(context) { SettingsStore(context) }
    val scope = rememberCoroutineScope()
    val connection by repo.connection.collectAsState()
    val storedMode by settings.operationMode.collectAsState(initial = OperationMode.COMPANION.name)
    var mode by remember(storedMode) { mutableStateOf(OperationMode.fromStored(storedMode)) }

    var totalPackets by remember { mutableLongStateOf(0L) }
    var packetsPerSecond by remember { mutableLongStateOf(0L) }
    var lastRssi by remember { mutableStateOf(-127) }
    var avgRssi by remember { mutableStateOf(-127) }
    var lastChannel by remember { mutableStateOf(0) }
    var mgmtFrames by remember { mutableLongStateOf(0L) }
    var dataFrames by remember { mutableLongStateOf(0L) }
    var deauthFrames by remember { mutableLongStateOf(0L) }
    var beaconFrames by remember { mutableLongStateOf(0L) }

    val bssidCounts = remember { mutableStateMapOf<String, Int>() }
    val srcCounts = remember { mutableStateMapOf<String, Int>() }
    val ppsWindow = remember { ArrayDeque<Long>() }
    val rssiWindow = remember { ArrayDeque<Int>() }

    LaunchedEffect(Unit) {
        repo.packets.collectLatest { p ->
            totalPackets += 1
            lastRssi = p.rssi
            lastChannel = p.channel

            val now = System.currentTimeMillis()
            ppsWindow.addLast(now)
            while (ppsWindow.isNotEmpty() && now - ppsWindow.first() > 1_000L) {
                ppsWindow.removeFirst()
            }
            packetsPerSecond = ppsWindow.size.toLong()

            rssiWindow.addLast(p.rssi)
            while (rssiWindow.size > 200) {
                rssiWindow.removeFirst()
            }
            avgRssi = if (rssiWindow.isEmpty()) -127 else (rssiWindow.average().roundToInt())

            when (p.type) {
                0 -> mgmtFrames += 1
                2 -> dataFrames += 1
            }
            if (p.type == 0 && p.subtype == 12) deauthFrames += 1
            if (p.type == 0 && p.subtype == 8) beaconFrames += 1

            if (p.bssid.isNotBlank()) {
                bssidCounts[p.bssid] = (bssidCounts[p.bssid] ?: 0) + 1
            }
            if (p.src.isNotBlank()) {
                srcCounts[p.src] = (srcCounts[p.src] ?: 0) + 1
            }
        }
    }

    val capabilities = remember(connection) { CapabilityDetector.detect(connection) }
    val topBssid = bssidCounts.maxByOrNull { it.value }?.key ?: "--"
    val uniqueAps = bssidCounts.size
    val uniqueClients = srcCounts.size

    ScreenScaffold(title = "CYD OPS") {
        LazyColumn(verticalArrangement = Arrangement.spacedBy(12.dp)) {
            item {
                NeonCard(header = "Mode") {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                        NeonButton(
                            text = "Companion",
                            onClick = {
                                mode = OperationMode.COMPANION
                                scope.launch { settings.setOperationMode(OperationMode.COMPANION.name) }
                            },
                            enabled = mode != OperationMode.COMPANION,
                            modifier = Modifier.weight(1f)
                        )
                        NeonButton(
                            text = "Standalone",
                            onClick = {
                                mode = OperationMode.STANDALONE
                                scope.launch { settings.setOperationMode(OperationMode.STANDALONE.name) }
                            },
                            enabled = mode != OperationMode.STANDALONE,
                            modifier = Modifier.weight(1f)
                        )
                    }

                    Spacer(Modifier.height(10.dp))
                    Row(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        CapabilityPill("VPN", capabilities.vpnCapture)
                        CapabilityPill("ROOT", capabilities.rootDetected)
                        CapabilityPill("RAW 802.11", capabilities.raw80211Capture)
                        CapabilityPill("LINK", capabilities.companionLinked)
                    }
                    Spacer(Modifier.height(8.dp))
                    Text(
                        text = "Mode: ${mode.name}  |  Authorized testing only.",
                        style = MaterialTheme.typography.bodySmall.copy(color = Color.LightGray)
                    )
                }
            }

            item {
                NeonCard(header = "Target Ops") {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column {
                            Text(
                                "TARGET BSSID",
                                style = MaterialTheme.typography.labelSmall.copy(
                                    color = MaterialTheme.colorScheme.primary.copy(alpha = 0.7f),
                                    letterSpacing = 1.sp
                                )
                            )
                            Text(
                                topBssid,
                                style = MaterialTheme.typography.bodyMedium.copy(
                                    color = Color.White,
                                    fontWeight = FontWeight.Bold
                                )
                            )
                        }
                        Column(horizontalAlignment = Alignment.End) {
                            StatChip("CH", if (lastChannel > 0) "$lastChannel" else "--")
                            Spacer(Modifier.height(6.dp))
                            StatChip("RSSI", "$lastRssi")
                        }
                    }
                    Spacer(Modifier.height(8.dp))
                    SignalBars(rssi = lastRssi)
                }
            }

            item {
                NeonCard(header = "Monitor") {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                        StatChip("PKTS", totalPackets.toString())
                        StatChip("PPS", packetsPerSecond.toString())
                        StatChip("AVG", "$avgRssi")
                    }
                    Spacer(Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                        StatChip("APS", uniqueAps.toString())
                        StatChip("CLIENTS", uniqueClients.toString())
                        StatChip("MODE", mode.name.take(4))
                    }
                }
            }

            item {
                NeonCard(header = "Automate") {
                    Text(
                        "Companion mode drives CYD automate engine.",
                        style = MaterialTheme.typography.bodySmall.copy(color = Color.LightGray)
                    )
                    Spacer(Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                        listOf("Y0INK", "RAW", "SCOPE", "JAMMIT").forEach { op ->
                            NeonButton(
                                text = op,
                                onClick = { repo.sendControlCommand("SET_MODE", mapOf("mode" to op)) },
                                enabled = mode == OperationMode.COMPANION && capabilities.companionLinked,
                                modifier = Modifier.weight(1f)
                            )
                        }
                    }
                }
            }

            item {
                NeonCard(header = "Recon") {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                        StatChip("MGMT", mgmtFrames.toString())
                        StatChip("DATA", dataFrames.toString())
                        StatChip("DEAUTH", deauthFrames.toString())
                        StatChip("BEACON", beaconFrames.toString())
                    }
                }
            }

            item {
                NeonCard(header = "Bruce") {
                    val actions = listOf(
                        "Deauth AP" to capabilities.raw80211Injection,
                        "Deauth All" to capabilities.raw80211Injection,
                        "Beacon Spam" to capabilities.raw80211Injection,
                        "Probe Flood" to capabilities.raw80211Injection
                    )
                    Text(
                        "Advanced radio actions are capability-gated.",
                        style = MaterialTheme.typography.bodySmall.copy(color = Color.LightGray)
                    )
                    Spacer(Modifier.height(8.dp))
                    actions.forEach { (name, enabled) ->
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(name, style = MaterialTheme.typography.bodyMedium.copy(color = Color.White))
                            CapabilityPill(if (enabled) "READY" else "BLOCKED", enabled)
                        }
                        Spacer(Modifier.height(6.dp))
                    }
                }
            }
        }
    }
}

@Composable
private fun CapabilityPill(label: String, enabled: Boolean) {
    val accent = if (enabled) MaterialTheme.colorScheme.secondary else Color.Gray
    Box(
        modifier = Modifier
            .background(accent.copy(alpha = 0.12f), shape = CutCornerShape(topStart = 4.dp, bottomEnd = 4.dp))
            .border(1.dp, accent.copy(alpha = 0.45f), shape = CutCornerShape(topStart = 4.dp, bottomEnd = 4.dp))
            .padding(horizontal = 8.dp, vertical = 4.dp)
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall.copy(
                color = accent,
                fontWeight = FontWeight.Bold,
                letterSpacing = 0.5.sp
            )
        )
    }
}

@Composable
private fun SignalBars(rssi: Int) {
    val bars = when {
        rssi >= -55 -> 5
        rssi >= -65 -> 4
        rssi >= -73 -> 3
        rssi >= -82 -> 2
        rssi >= -90 -> 1
        else -> 0
    }
    Row(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalAlignment = Alignment.Bottom) {
        val heights = listOf(6.dp, 10.dp, 14.dp, 18.dp, 22.dp)
        heights.forEachIndexed { index, height ->
            val active = index < bars
            Box(
                modifier = Modifier
                    .size(width = 8.dp, height = height)
                    .background(
                        if (active) MaterialTheme.colorScheme.secondary else Color.Gray.copy(alpha = 0.3f),
                        shape = CutCornerShape(topStart = 2.dp, bottomEnd = 2.dp)
                    )
            )
        }
        Spacer(Modifier.size(4.dp))
        Text(
            text = "$rssi dBm",
            style = MaterialTheme.typography.labelSmall.copy(
                color = Color.LightGray,
                fontSize = 11.sp
            )
        )
    }
}
