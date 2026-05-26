#pragma once

#include <Arduino.h>
#include <FS.h>

// NEONDRIVE → Wardrive Analyzer session bundle staging and Dropbox sync.
//
// Workflow:
//   1. Technician runs a capture module (SP3CTER / Y0INK / etc.).
//   2. GET /analyzer shows the sync page.
//   3. POST /api/analyzer/stage builds a session bundle on SD.
//   4. POST /api/analyzer/sync uploads the bundle to Dropbox.
//   5. Wardrive Analyzer PC app imports the bundle manually.
//
// SD layout:  /neondrive/sessions/<session_id>/
//               manifest.json, summary.json, events.csv,
//               artifacts/*, logs/*, sync_complete.flag (last)
//
// Dropbox:    <cfg.dropbox_folder>/neondrive/incoming/<device_id>/
//               sessions/<session_id>/ (same tree)

namespace AnalyzerSync {

// ── Sync state ────────────────────────────────────────────────────────────────

struct SyncState {
    char session_id[64];       // e.g. "ND-CYD35-0001-SP3CTER-T0000300"
    char local_root[96];       // SD path: "/neondrive/sessions/<session_id>"
    char module_name[16];      // "SP3CTER" | "Y0INK" | "NONE" | etc.
    char sync_status[16];      // "none"|"staged"|"syncing"|"synced"|"failed"
    char last_error[128];
    uint32_t staged_at_ms;
    int  files_uploaded;
    int  files_total;
};

extern SyncState g_state;

// ── Stage parameters passed from main.cpp context ────────────────────────────

struct StageParams {
    const char* capture_dir;   // current captureDir e.g. "/captures/SSID_AA-BB-CC"
    const char* module;        // autoModeStr result
    const char* ssid;
    const char* bssid;
    int         channel;
    const char* security;      // auth mode string
    int         pmkid_count;
    int         handshake_count;
    bool        has_target;
    String      dropbox_folder; // cfg.dropbox_folder
};

// ── Public API ────────────────────────────────────────────────────────────────

// Compile-time board code string (from ND_PROFILE_CODE).
const char* boardCode();

// Device identifier: "ND-{BOARD}-{MAC_SUFFIX}" (e.g. "ND-CYD35-A91B58").
// Requires WiFi to be initialized so macAddress() works.
String deviceId();

// Build a session bundle on SD from the current capture directory.
// Sets g_state.session_id, g_state.local_root, g_state.sync_status.
// Returns true on success.
bool stageSession(fs::FS& sd, const StageParams& p);

// Upload a previously staged session bundle to Dropbox.
// Uploads manifest → summary → events → artifacts → logs → flag (last).
// Returns true if all required files uploaded and flag was written.
bool uploadSession(fs::FS& sd,
                   const char* dropbox_token,
                   const String& dropbox_folder);

// JSON for GET /api/analyzer/status.
String statusJson(bool dropbox_configured, const String& dropbox_folder);

// JSON array of session folders found under /neondrive/sessions/ on SD.
String sessionsJson(fs::FS& sd);

// Raw JSON content of manifest.json for a given session_id (or error JSON).
String sessionManifestJson(fs::FS& sd, const char* session_id);

} // namespace AnalyzerSync
