package com.example.cydcompanion.data

import android.content.Context
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

private val Context.settingsDataStore by preferencesDataStore(name = "settings")

class SettingsStore(private val context: Context) {

    private object Keys {
        val CYD_IP = stringPreferencesKey("cyd_ip")
        val OPERATION_MODE = stringPreferencesKey("operation_mode")
        val LAUNCH_TAB = stringPreferencesKey("launch_tab")
        val AUTO_RECONNECT = booleanPreferencesKey("auto_reconnect")
        val KEEP_SCREEN_AWAKE = booleanPreferencesKey("keep_screen_awake")
        val SHOW_LEGAL_DISCLAIMER = booleanPreferencesKey("show_legal_disclaimer")
        val HAPTIC_FEEDBACK = booleanPreferencesKey("haptic_feedback")
        val ENABLE_ANIMATIONS = booleanPreferencesKey("enable_animations")
        val SHOW_HYPERCUBE = booleanPreferencesKey("show_hypercube")
        val COMPACT_UI = booleanPreferencesKey("compact_ui")

        val PREFER_USB = booleanPreferencesKey("prefer_usb")
        val PACKET_POLL_MS = intPreferencesKey("packet_poll_ms")
        val CONSOLE_POLL_MS = intPreferencesKey("console_poll_ms")
        val PACKET_BUFFER_MAX = intPreferencesKey("packet_buffer_max")
        val CONSOLE_BUFFER_MAX = intPreferencesKey("console_buffer_max")
        val AUTO_SYNC_BADGE = booleanPreferencesKey("auto_sync_badge")

        val AI_BACKEND_TYPE = stringPreferencesKey("ai_backend_type")
        val OLLAMA_URL = stringPreferencesKey("ollama_url")
        val OLLAMA_MODEL = stringPreferencesKey("ollama_model")
        val OPENAI_MODEL = stringPreferencesKey("openai_model")
        val AI_AUTO_REFINE = booleanPreferencesKey("ai_auto_refine")

        val VAULT_PROJECT_ID = stringPreferencesKey("vault_project_id")
        val VAULT_CAPTURE_ID = stringPreferencesKey("vault_capture_id")
        val VAULT_CAPTURE_LABEL = stringPreferencesKey("vault_capture_label")
        val AUTO_PULL_ALLOWED_ONLY = booleanPreferencesKey("auto_pull_allowed_only")
        val MAX_IMPORT_MB = intPreferencesKey("max_import_mb")
        val EXPORT_INCLUDE_METADATA = booleanPreferencesKey("export_include_metadata")

        val WPASEC_API_KEY = stringPreferencesKey("wpasec_api_key")
        val MASK_MAC_ADDRESSES = booleanPreferencesKey("mask_mac_addresses")
    }

    val cydIp: Flow<String> = context.settingsDataStore.data.map { prefs: Preferences ->
        prefs[Keys.CYD_IP] ?: "192.168.4.1"
    }

    suspend fun setCydIp(ip: String) {
        context.settingsDataStore.edit { it[Keys.CYD_IP] = ip.trim() }
    }

    /** "COMPANION" | "STANDALONE" (default is COMPANION). */
    val operationMode: Flow<String> = context.settingsDataStore.data.map { prefs: Preferences ->
        prefs[Keys.OPERATION_MODE] ?: "COMPANION"
    }

    suspend fun setOperationMode(mode: String) {
        context.settingsDataStore.edit { it[Keys.OPERATION_MODE] = mode.trim().uppercase() }
    }

    val launchTab: Flow<String> = context.settingsDataStore.data.map { prefs: Preferences ->
        prefs[Keys.LAUNCH_TAB] ?: "live"
    }

    suspend fun setLaunchTab(tab: String) {
        context.settingsDataStore.edit { it[Keys.LAUNCH_TAB] = tab.trim().lowercase() }
    }

    val autoReconnect: Flow<Boolean> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.AUTO_RECONNECT] ?: true
    }

    suspend fun setAutoReconnect(enabled: Boolean) {
        context.settingsDataStore.edit { it[Keys.AUTO_RECONNECT] = enabled }
    }

    val keepScreenAwake: Flow<Boolean> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.KEEP_SCREEN_AWAKE] ?: false
    }

    suspend fun setKeepScreenAwake(enabled: Boolean) {
        context.settingsDataStore.edit { it[Keys.KEEP_SCREEN_AWAKE] = enabled }
    }

    val showLegalDisclaimer: Flow<Boolean> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.SHOW_LEGAL_DISCLAIMER] ?: true
    }

    suspend fun setShowLegalDisclaimer(enabled: Boolean) {
        context.settingsDataStore.edit { it[Keys.SHOW_LEGAL_DISCLAIMER] = enabled }
    }

    val hapticFeedback: Flow<Boolean> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.HAPTIC_FEEDBACK] ?: true
    }

    suspend fun setHapticFeedback(enabled: Boolean) {
        context.settingsDataStore.edit { it[Keys.HAPTIC_FEEDBACK] = enabled }
    }

    val enableAnimations: Flow<Boolean> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.ENABLE_ANIMATIONS] ?: true
    }

    suspend fun setEnableAnimations(enabled: Boolean) {
        context.settingsDataStore.edit { it[Keys.ENABLE_ANIMATIONS] = enabled }
    }

    val showHypercube: Flow<Boolean> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.SHOW_HYPERCUBE] ?: true
    }

    suspend fun setShowHypercube(enabled: Boolean) {
        context.settingsDataStore.edit { it[Keys.SHOW_HYPERCUBE] = enabled }
    }

    val compactUi: Flow<Boolean> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.COMPACT_UI] ?: false
    }

    suspend fun setCompactUi(enabled: Boolean) {
        context.settingsDataStore.edit { it[Keys.COMPACT_UI] = enabled }
    }

    val preferUsb: Flow<Boolean> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.PREFER_USB] ?: false
    }

    suspend fun setPreferUsb(enabled: Boolean) {
        context.settingsDataStore.edit { it[Keys.PREFER_USB] = enabled }
    }

    val packetPollMs: Flow<Int> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.PACKET_POLL_MS] ?: 260
    }

    suspend fun setPacketPollMs(ms: Int) {
        context.settingsDataStore.edit { it[Keys.PACKET_POLL_MS] = ms.coerceIn(100, 5_000) }
    }

    val consolePollMs: Flow<Int> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.CONSOLE_POLL_MS] ?: 450
    }

    suspend fun setConsolePollMs(ms: Int) {
        context.settingsDataStore.edit { it[Keys.CONSOLE_POLL_MS] = ms.coerceIn(100, 5_000) }
    }

    val packetBufferMax: Flow<Int> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.PACKET_BUFFER_MAX] ?: 500
    }

    suspend fun setPacketBufferMax(max: Int) {
        context.settingsDataStore.edit { it[Keys.PACKET_BUFFER_MAX] = max.coerceIn(100, 10_000) }
    }

    val consoleBufferMax: Flow<Int> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.CONSOLE_BUFFER_MAX] ?: 200
    }

    suspend fun setConsoleBufferMax(max: Int) {
        context.settingsDataStore.edit { it[Keys.CONSOLE_BUFFER_MAX] = max.coerceIn(50, 5_000) }
    }

    val autoSyncBadge: Flow<Boolean> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.AUTO_SYNC_BADGE] ?: true
    }

    suspend fun setAutoSyncBadge(enabled: Boolean) {
        context.settingsDataStore.edit { it[Keys.AUTO_SYNC_BADGE] = enabled }
    }

    /** "Ollama" | "OpenAI" (default is Ollama). */
    val aiBackendType: Flow<String> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.AI_BACKEND_TYPE] ?: "Ollama"
    }

    suspend fun setAiBackendType(type: String) {
        context.settingsDataStore.edit { it[Keys.AI_BACKEND_TYPE] = type.trim() }
    }

    val ollamaUrl: Flow<String> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.OLLAMA_URL] ?: "http://localhost:11434"
    }

    suspend fun setOllamaUrl(url: String) {
        context.settingsDataStore.edit { it[Keys.OLLAMA_URL] = url.trim() }
    }

    val ollamaModel: Flow<String> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.OLLAMA_MODEL] ?: "mistral"
    }

    suspend fun setOllamaModel(model: String) {
        context.settingsDataStore.edit { it[Keys.OLLAMA_MODEL] = model.trim() }
    }

    val openAiModel: Flow<String> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.OPENAI_MODEL] ?: "gpt-4o-mini"
    }

    suspend fun setOpenAiModel(model: String) {
        context.settingsDataStore.edit { it[Keys.OPENAI_MODEL] = model.trim() }
    }

    val aiAutoRefine: Flow<Boolean> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.AI_AUTO_REFINE] ?: true
    }

    suspend fun setAiAutoRefine(enabled: Boolean) {
        context.settingsDataStore.edit { it[Keys.AI_AUTO_REFINE] = enabled }
    }

    /**
     * One-time migration for older builds that stored a personal/local IP as the Ollama default.
     * Keeps explicit user-entered values untouched unless they match known legacy defaults.
     */
    suspend fun migrateAiDefaultsIfLegacy() {
        context.settingsDataStore.edit { prefs ->
            val oldUrl = prefs[Keys.OLLAMA_URL].orEmpty().trim()
            val oldModel = prefs[Keys.OLLAMA_MODEL].orEmpty().trim()

            if (oldUrl.isBlank() || oldUrl == "http://192.168.1.100:11434" || oldUrl == "http://192.168.1.227:11434") {
                prefs[Keys.OLLAMA_URL] = "http://localhost:11434"
            }
            if (oldModel.isBlank() || oldModel == "llama3.2") {
                prefs[Keys.OLLAMA_MODEL] = "mistral"
            }
        }
    }

    // ---------------------------------------------------------------------
    // Vault (projects/captures)
    // ---------------------------------------------------------------------

    val vaultProjectId: Flow<String> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.VAULT_PROJECT_ID] ?: "default"
    }

    suspend fun setVaultProjectId(projectId: String) {
        context.settingsDataStore.edit { it[Keys.VAULT_PROJECT_ID] = projectId.trim().ifBlank { "default" } }
    }

    val vaultCaptureId: Flow<String?> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.VAULT_CAPTURE_ID]
    }

    suspend fun setVaultCaptureId(captureId: String?) {
        context.settingsDataStore.edit {
            if (captureId.isNullOrBlank()) it.remove(Keys.VAULT_CAPTURE_ID)
            else it[Keys.VAULT_CAPTURE_ID] = captureId.trim()
        }
    }

    val vaultCaptureLabel: Flow<String?> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.VAULT_CAPTURE_LABEL]
    }

    suspend fun setVaultCaptureLabel(label: String?) {
        context.settingsDataStore.edit {
            if (label.isNullOrBlank()) it.remove(Keys.VAULT_CAPTURE_LABEL)
            else it[Keys.VAULT_CAPTURE_LABEL] = label.trim()
        }
    }

    val autoPullAllowedOnly: Flow<Boolean> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.AUTO_PULL_ALLOWED_ONLY] ?: true
    }

    suspend fun setAutoPullAllowedOnly(enabled: Boolean) {
        context.settingsDataStore.edit { it[Keys.AUTO_PULL_ALLOWED_ONLY] = enabled }
    }

    val maxImportMb: Flow<Int> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.MAX_IMPORT_MB] ?: 256
    }

    suspend fun setMaxImportMb(limitMb: Int) {
        context.settingsDataStore.edit { it[Keys.MAX_IMPORT_MB] = limitMb.coerceIn(16, 4_096) }
    }

    val exportIncludeMetadata: Flow<Boolean> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.EXPORT_INCLUDE_METADATA] ?: true
    }

    suspend fun setExportIncludeMetadata(enabled: Boolean) {
        context.settingsDataStore.edit { it[Keys.EXPORT_INCLUDE_METADATA] = enabled }
    }
    
    // ---------------------------------------------------------------------
    // WPA-SEC
    // ---------------------------------------------------------------------
    
    val wpasecApiKey: Flow<String> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.WPASEC_API_KEY] ?: ""
    }
    
    suspend fun setWpasecApiKey(key: String) {
        context.settingsDataStore.edit { it[Keys.WPASEC_API_KEY] = key.trim() }
    }

    val maskMacAddresses: Flow<Boolean> = context.settingsDataStore.data.map { prefs ->
        prefs[Keys.MASK_MAC_ADDRESSES] ?: false
    }

    suspend fun setMaskMacAddresses(enabled: Boolean) {
        context.settingsDataStore.edit { it[Keys.MASK_MAC_ADDRESSES] = enabled }
    }
}
