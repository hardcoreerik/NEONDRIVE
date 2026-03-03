package com.example.cydcompanion.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.cydcompanion.data.*
import kotlinx.coroutines.launch
import java.io.File

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
fun DataScreen(repo: CydRepository) {
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    val settings = remember { SettingsStore(context) }
    val vault = remember { VaultManager(context) }
    val analyzer = remember { PcapAnalyzer() }
    
    val vaultProjectId by settings.vaultProjectId.collectAsState(initial = "default")
    val vaultCaptureId by settings.vaultCaptureId.collectAsState(initial = null)
    val savedWpaSecApiKey by settings.wpasecApiKey.collectAsState(initial = "")
    
    // WPA-SEC state
    var wpaSecApiKey by remember { mutableStateOf("") }
    var wpaSecStatus by remember { mutableStateOf("") }
    var isUploading by remember { mutableStateOf(false) }
    var uploadProgress by remember { mutableStateOf("") }
    var crackedList by remember { mutableStateOf<List<WpaSecCrack>>(emptyList()) }
    var isLoadingCracks by remember { mutableStateOf(false) }
    
    // PCAP Analysis state
    var pcapFiles by remember { mutableStateOf<List<File>>(emptyList()) }
    var selectedPcap by remember { mutableStateOf<File?>(null) }
    var pcapAnalysis by remember { mutableStateOf<PcapAnalysis?>(null) }
    var isAnalyzing by remember { mutableStateOf(false) }
    
    // Load WPA-SEC API key from settings
    LaunchedEffect(savedWpaSecApiKey) {
        wpaSecApiKey = savedWpaSecApiKey
        
        // Load vault PCAPs
        if (!vaultCaptureId.isNullOrBlank()) {
            val captureDir = vault.captureDir(vaultProjectId, vaultCaptureId!!)
            pcapFiles = captureDir.walk()
                .filter { it.isFile && (it.extension == "pcap" || it.extension == "22000") }
                .toList()
        }
    }
    
    fun uploadAllPcaps() {
        if (wpaSecApiKey.isBlank()) {
            wpaSecStatus = "⚠ Enter API key first"
            return
        }
        
        if (pcapFiles.isEmpty()) {
            wpaSecStatus = "⚠ No PCAP files in active capture"
            return
        }
        
        isUploading = true
        wpaSecStatus = ""
        
        scope.launch {
            val wpaRepo = WpaSecRepository(wpaSecApiKey)
            var uploaded = 0
            var skipped = 0
            var errors = 0
            
            pcapFiles.forEachIndexed { idx, file ->
                uploadProgress = "Uploading ${idx + 1}/${pcapFiles.size}: ${file.name}"
                
                val result = wpaRepo.uploadCapture(file) { status ->
                    uploadProgress = status
                }
                
                when {
                    result.success && result.uploaded > 0 -> uploaded++
                    result.success && result.alreadyExists > 0 -> skipped++
                    else -> errors++
                }
                
                kotlinx.coroutines.delay(500) // Rate limit
            }
            
            isUploading = false
            uploadProgress = ""
            wpaSecStatus = "✓ Uploaded: $uploaded | Skipped: $skipped | Errors: $errors"
            
            // Auto-refresh cracks
            isLoadingCracks = true
            crackedList = wpaRepo.downloadPotfile()
            isLoadingCracks = false
            
            // Save to vault for AI
            if (crackedList.isNotEmpty() && !vaultCaptureId.isNullOrBlank()) {
                val cracksText = buildString {
                    appendLine("# WPA-SEC Cracked Passwords")
                    appendLine("# Retrieved: ${System.currentTimeMillis()}")
                    appendLine()
                    crackedList.forEach {
                        appendLine("${it.bssid} | ${it.ssid} | ${it.password}")
                    }
                }
                vault.saveBytes(
                    vaultProjectId,
                    vaultCaptureId!!,
                    "wpasec_results.txt",
                    cracksText.toByteArray()
                )
            }
        }
    }
    
    fun refreshCracks() {
        if (wpaSecApiKey.isBlank()) {
            wpaSecStatus = "⚠ Enter API key first"
            return
        }
        
        isLoadingCracks = true
        scope.launch {
            val wpaRepo = WpaSecRepository(wpaSecApiKey)
            crackedList = wpaRepo.downloadPotfile()
            isLoadingCracks = false
            wpaSecStatus = "✓ Loaded ${crackedList.size} cracked passwords"
        }
    }
    
    fun analyzePcap(file: File) {
        selectedPcap = file
        isAnalyzing = true
        scope.launch {
            pcapAnalysis = analyzer.analyze(file)
            isAnalyzing = false
        }
    }

    ScreenScaffold(title = "DATA ANALYSIS") {
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            
            // ══════════════════════════════════════════════════════════════
            // WPA-SEC SECTION
            // ══════════════════════════════════════════════════════════════
            item {
                NeonCard(title = "WPA-SEC UPLOADER", accentColor = neonMagenta) {
                    Text(
                        "Upload handshakes to wpa-sec.stanev.org for cloud cracking",
                        color = Color.Gray,
                        fontSize = 11.sp
                    )
                    Spacer(Modifier.height(8.dp))
                    
                    // API Key input
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        NeonTextField(
                            value = wpaSecApiKey,
                            onValueChange = { wpaSecApiKey = it },
                            label = "WPA-SEC API Key",
                            modifier = Modifier.weight(1f)
                        )
                        OutlinedButton(
                            onClick = {
                                scope.launch {
                                    settings.setWpasecApiKey(wpaSecApiKey)
                                    wpaSecStatus = "✓ API key saved"
                                    kotlinx.coroutines.delay(2000)
                                    wpaSecStatus = ""
                                }
                            },
                            colors = ButtonDefaults.outlinedButtonColors(
                                contentColor = neonMagenta
                            )
                        ) {
                            Text("SAVE", fontWeight = FontWeight.Bold, fontSize = 11.sp)
                        }
                    }
                    
                    Spacer(Modifier.height(8.dp))
                    
                    // Status display
                    if (wpaSecStatus.isNotEmpty()) {
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .background(
                                    when {
                                        wpaSecStatus.startsWith("✓") -> neonLime.copy(alpha = 0.1f)
                                        wpaSecStatus.startsWith("⚠") -> neonAmber.copy(alpha = 0.1f)
                                        wpaSecStatus.startsWith("✗") -> neonRed.copy(alpha = 0.1f)
                                        else -> neonCyan.copy(alpha = 0.1f)
                                    },
                                    shape = androidx.compose.foundation.shape.RoundedCornerShape(6.dp)
                                )
                                .border(
                                    1.dp,
                                    when {
                                        wpaSecStatus.startsWith("✓") -> neonLime.copy(alpha = 0.3f)
                                        wpaSecStatus.startsWith("⚠") -> neonAmber.copy(alpha = 0.3f)
                                        wpaSecStatus.startsWith("✗") -> neonRed.copy(alpha = 0.3f)
                                        else -> neonCyan.copy(alpha = 0.3f)
                                    },
                                    shape = androidx.compose.foundation.shape.RoundedCornerShape(6.dp)
                                )
                                .padding(12.dp)
                        ) {
                            Text(
                                wpaSecStatus,
                                color = when {
                                    wpaSecStatus.startsWith("✓") -> neonLime
                                    wpaSecStatus.startsWith("⚠") -> neonAmber
                                    wpaSecStatus.startsWith("✗") -> neonRed
                                    else -> neonCyan
                                },
                                fontSize = 12.sp,
                                fontWeight = FontWeight.SemiBold
                            )
                        }
                    }
                    
                    if (isUploading && uploadProgress.isNotEmpty()) {
                        Spacer(Modifier.height(8.dp))
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            CircularProgressIndicator(
                                modifier = Modifier.size(16.dp),
                                strokeWidth = 2.dp,
                                color = neonMagenta
                            )
                            Spacer(Modifier.width(8.dp))
                            Text(
                                uploadProgress,
                                color = Color.LightGray,
                                fontSize = 11.sp
                            )
                        }
                    }
                    
                    Spacer(Modifier.height(12.dp))
                    
                    // Action buttons
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Button(
                            onClick = ::uploadAllPcaps,
                            enabled = !isUploading && wpaSecApiKey.isNotBlank() && pcapFiles.isNotEmpty(),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = neonMagenta.copy(alpha = 0.15f),
                                contentColor = neonMagenta
                            ),
                            modifier = Modifier.weight(1f)
                        ) {
                            Icon(Icons.Filled.CloudUpload, contentDescription = null, modifier = Modifier.size(16.dp))
                            Spacer(Modifier.width(6.dp))
                            Text("UPLOAD ALL", fontWeight = FontWeight.Bold, fontSize = 12.sp)
                        }
                        
                        OutlinedButton(
                            onClick = ::refreshCracks,
                            enabled = !isLoadingCracks && wpaSecApiKey.isNotBlank(),
                            colors = ButtonDefaults.outlinedButtonColors(
                                contentColor = neonCyan
                            ),
                            modifier = Modifier.weight(1f)
                        ) {
                            Icon(Icons.Filled.Refresh, contentDescription = null, modifier = Modifier.size(16.dp))
                            Spacer(Modifier.width(6.dp))
                            Text("REFRESH", fontWeight = FontWeight.Bold, fontSize = 12.sp)
                        }
                    }
                    
                    Spacer(Modifier.height(8.dp))
                    
                    Text(
                        "${pcapFiles.size} PCAP/22000 files in vault | ${crackedList.size} cracked",
                        color = Color.Gray,
                        fontSize = 10.sp
                    )
                }
            }
            
            // ══════════════════════════════════════════════════════════════
            // CRACKED PASSWORDS
            // ══════════════════════════════════════════════════════════════
            if (crackedList.isNotEmpty()) {
                item {
                    NeonCard(title = "CRACKED PASSWORDS", accentColor = neonLime) {
                        Text(
                            "${crackedList.size} networks cracked by WPA-SEC",
                            color = neonLime,
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Bold
                        )
                        Spacer(Modifier.height(8.dp))
                    }
                }
                
                items(crackedList.take(50)) { crack ->
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { /* Copy to clipboard? */ },
                        color = cardDark,
                        shape = androidx.compose.foundation.shape.RoundedCornerShape(6.dp)
                    ) {
                        Column(
                            modifier = Modifier.padding(12.dp)
                        ) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween
                            ) {
                                Text(
                                    crack.ssid,
                                    color = Color.White,
                                    fontSize = 14.sp,
                                    fontWeight = FontWeight.Bold
                                )
                                Icon(
                                    Icons.Filled.Check,
                                    contentDescription = null,
                                    tint = neonLime,
                                    modifier = Modifier.size(18.dp)
                                )
                            }
                            Spacer(Modifier.height(4.dp))
                            Text(
                                crack.bssid,
                                color = Color.Gray,
                                fontSize = 10.sp
                            )
                            Spacer(Modifier.height(6.dp))
                            Text(
                                "Password: ${crack.password}",
                                color = neonLime,
                                fontSize = 12.sp,
                                fontWeight = FontWeight.SemiBold
                            )
                        }
                    }
                }
            }
            
            // ══════════════════════════════════════════════════════════════
            // PCAP ANALYZER
            // ══════════════════════════════════════════════════════════════
            item {
                NeonCard(title = "PCAP ANALYZER", accentColor = neonCyan) {
                    Text(
                        "Analyze capture files in active vault",
                        color = Color.Gray,
                        fontSize = 11.sp
                    )
                    Spacer(Modifier.height(8.dp))
                    
                    if (pcapFiles.isEmpty()) {
                        Text(
                            "No PCAP files in active capture. Go to FILE tab to import.",
                            color = Color.Gray,
                            fontSize = 11.sp,
                            fontStyle = androidx.compose.ui.text.font.FontStyle.Italic
                        )
                    } else {
                        Text(
                            "${pcapFiles.size} files available",
                            color = neonCyan,
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Bold
                        )
                    }
                }
            }
            
            items(pcapFiles) { file ->
                Surface(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { analyzePcap(file) },
                    color = if (selectedPcap == file) neonCyan.copy(alpha = 0.1f) else cardDark,
                    shape = androidx.compose.foundation.shape.RoundedCornerShape(6.dp)
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(12.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                file.name,
                                color = Color.White,
                                fontSize = 12.sp,
                                fontWeight = FontWeight.SemiBold
                            )
                            Spacer(Modifier.height(4.dp))
                            Text(
                                "${file.length() / 1024} KB",
                                color = Color.Gray,
                                fontSize = 10.sp
                            )
                        }
                        Icon(
                            Icons.Filled.Analytics,
                            contentDescription = null,
                            tint = if (selectedPcap == file) neonCyan else Color.Gray,
                            modifier = Modifier.size(20.dp)
                        )
                    }
                }
            }
            
            // ══════════════════════════════════════════════════════════════
            // PCAP ANALYSIS RESULTS
            // ══════════════════════════════════════════════════════════════
            if (pcapAnalysis != null) {
                item {
                    NeonCard(
                        title = "ANALYSIS: ${pcapAnalysis!!.fileName}",
                        accentColor = neonAmber
                    ) {
                        if (isAnalyzing) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                CircularProgressIndicator(
                                    modifier = Modifier.size(16.dp),
                                    strokeWidth = 2.dp,
                                    color = neonAmber
                                )
                                Spacer(Modifier.width(8.dp))
                                Text(
                                    "Analyzing...",
                                    color = neonAmber,
                                    fontSize = 12.sp
                                )
                            }
                        } else {
                            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                                AnalysisRow("Type", pcapAnalysis!!.captureType, 
                                    if (pcapAnalysis!!.hasEapol) neonLime else Color.LightGray)
                                AnalysisRow("Packets", pcapAnalysis!!.packetCount.toString(), Color.LightGray)
                                AnalysisRow("EAPOL Frames", pcapAnalysis!!.eapolCount.toString(),
                                    if (pcapAnalysis!!.eapolCount >= 4) neonLime else neonAmber)
                                if (pcapAnalysis!!.bssids.isNotEmpty()) {
                                    AnalysisRow("BSSIDs", pcapAnalysis!!.bssids.size.toString(), Color.LightGray)
                                    Spacer(Modifier.height(4.dp))
                                    pcapAnalysis!!.bssids.take(5).forEach { bssid ->
                                        Text(
                                            "  • $bssid",
                                            color = Color.Gray,
                                            fontSize = 10.sp
                                        )
                                    }
                                }
                                Spacer(Modifier.height(4.dp))
                                Text(
                                    pcapAnalysis!!.summary,
                                    color = Color.LightGray,
                                    fontSize = 11.sp,
                                    fontStyle = androidx.compose.ui.text.font.FontStyle.Italic
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun NeonCard(
    title: String,
    accentColor: Color,
    content: @Composable ColumnScope.() -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = cardDark,
        shape = androidx.compose.foundation.shape.RoundedCornerShape(8.dp)
    ) {
        Column(
            modifier = Modifier.padding(16.dp)
        ) {
            Text(
                text = title,
                color = accentColor,
                fontSize = 13.sp,
                fontWeight = FontWeight.Black,
                letterSpacing = 1.5.sp
            )
            Spacer(Modifier.height(12.dp))
            content()
        }
    }
}

@Composable
private fun AnalysisRow(label: String, value: String, valueColor: Color) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(
            label,
            color = Color.Gray,
            fontSize = 11.sp
        )
        Text(
            value,
            color = valueColor,
            fontSize = 12.sp,
            fontWeight = FontWeight.Bold
        )
    }
}

@Composable
private fun NeonTextField(
    value: String,
    onValueChange: (String) -> Unit,
    label: String,
    modifier: Modifier = Modifier
) {
    OutlinedTextField(
        value = value,
        onValueChange = onValueChange,
        label = { Text(label, fontSize = 11.sp) },
        modifier = modifier.fillMaxWidth(),
        singleLine = true,
        colors = OutlinedTextFieldDefaults.colors(
            focusedBorderColor = neonMagenta,
            unfocusedBorderColor = Color.Gray.copy(alpha = 0.5f),
            focusedLabelColor = neonMagenta,
            unfocusedLabelColor = Color.Gray,
            cursorColor = neonMagenta
        )
    )
}
