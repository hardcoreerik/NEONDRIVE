#include "analyzer_sync.h"
#include "dropbox_client.h"
#include "device_profile_select.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <SD.h>

namespace AnalyzerSync {

// ── Persistent state ─────────────────────────────────────────────────────────

SyncState g_state = {};

// ── Internal constants ────────────────────────────────────────────────────────

static constexpr const char* kSessionsRoot = "/neondrive/sessions";
static constexpr const char* kCounterPath  = "/neondrive/session_counter.txt";

// ── Internal helpers ──────────────────────────────────────────────────────────

// Read the persistent session counter from SD. Returns 0 if absent.
static uint32_t loadCounter(fs::FS& sd) {
    if (!sd.exists(kCounterPath)) return 0;
    File f = sd.open(kCounterPath, FILE_READ);
    if (!f) return 0;
    String s = f.readStringUntil('\n');
    f.close();
    return (uint32_t)s.toInt();
}

// Increment and persist the counter. Returns the new value.
static uint32_t nextCounter(fs::FS& sd) {
    uint32_t n = loadCounter(sd) + 1;
    if (!sd.exists("/neondrive")) sd.mkdir("/neondrive");
    File f = sd.open(kCounterPath, FILE_WRITE);
    if (f) { f.println(n); f.close(); }
    return n;
}

// Safely copy a file from src to dst. Returns true if dst ends up on SD.
static bool safeCopy(fs::FS& sd, const char* src, const char* dst) {
    if (!sd.exists(src)) return false;
    File in = sd.open(src, FILE_READ);
    if (!in || in.isDirectory()) { if (in) in.close(); return false; }
    File out = sd.open(dst, FILE_WRITE);
    if (!out) { in.close(); return false; }
    static uint8_t buf[512];
    size_t n;
    while ((n = in.read(buf, sizeof(buf))) > 0) out.write(buf, n);
    in.close();
    out.close();
    return true;
}

// Write a string to a file, overwriting if it exists.
static bool writeFile(fs::FS& sd, const char* path, const String& content) {
    File f = sd.open(path, FILE_WRITE);
    if (!f) return false;
    f.print(content);
    f.close();
    return true;
}

// Read a file and return its content. Returns "" on failure.
static String readFile(fs::FS& sd, const char* path) {
    if (!sd.exists(path)) return "";
    File f = sd.open(path, FILE_READ);
    if (!f) return "";
    String s = f.readString();
    f.close();
    return s;
}

// Escape a string for inclusion inside a JSON double-quoted value.
static String jsonEsc(const char* s) {
    String out;
    for (const char* p = s; *p; ++p) {
        if (*p == '"' || *p == '\\') out += '\\';
        if (*p == '\n') { out += "\\n"; continue; }
        if (*p == '\r') { out += "\\r"; continue; }
        out += *p;
    }
    return out;
}

// Derive a quick PASS/WARNING/FAIL verdict from session evidence.
static const char* quickVerdict(const StageParams& p) {
    if (!p.has_target) return "INCONCLUSIVE";
    bool credentialEvidence = (p.pmkid_count > 0 || p.handshake_count >= 4);
    bool partialEvidence    = (p.handshake_count > 0 && p.handshake_count < 4);
    if (credentialEvidence) return "FAIL";
    if (partialEvidence)    return "WARNING";
    return "PASS";
}

// Auth mode string → security label.
static String securityLabel(const char* s) {
    if (!s || !*s) return "UNKNOWN";
    return String(s);
}

// ── Public API ────────────────────────────────────────────────────────────────

const char* boardCode() {
    return ND_PROFILE_CODE;
}

String deviceId() {
    String mac = WiFi.macAddress();  // "AA:BB:CC:DD:EE:FF"
    // Take last 3 bytes, strip colons → "DDEEFF"
    String suffix = "";
    if (mac.length() >= 17) {
        suffix += mac.substring(9, 11);
        suffix += mac.substring(12, 14);
        suffix += mac.substring(15, 17);
    }
    suffix.toUpperCase();
    String board = String(boardCode());
    board.toUpperCase();
    board.replace("_", "");  // "CYD35" not "CYD_35"
    return "ND-" + board + "-" + suffix;
}

bool stageSession(fs::FS& sd, const StageParams& p) {
    // Reset state.
    memset(&g_state, 0, sizeof(g_state));
    strncpy(g_state.sync_status, "none", sizeof(g_state.sync_status) - 1);

    if (!sd.exists("/neondrive")) sd.mkdir("/neondrive");

    // Session counter and ID.
    uint32_t seq = nextCounter(sd);
    uint32_t sec = (uint32_t)(millis() / 1000UL);

    // Build board-name token (uppercase, no underscores).
    char board[24] = {};
    strncpy(board, boardCode(), sizeof(board) - 1);
    for (char* c = board; *c; ++c) {
        *c = toupper((unsigned char)*c);
        if (*c == '_') *c = '\0';  // truncate at first underscore for brevity
        if (*c == '\0') break;
    }

    // Module name token.
    const char* mod = (p.module && p.module[0]) ? p.module : "NONE";

    snprintf(g_state.session_id, sizeof(g_state.session_id),
             "ND-%s-%04lu-%s-T%07lu", board, (unsigned long)seq, mod, (unsigned long)sec);
    snprintf(g_state.local_root, sizeof(g_state.local_root),
             "%s/%s", kSessionsRoot, g_state.session_id);
    strncpy(g_state.module_name, mod, sizeof(g_state.module_name) - 1);
    g_state.staged_at_ms = millis();

    // Create session directory tree.
    if (!sd.exists(kSessionsRoot)) sd.mkdir(kSessionsRoot);
    sd.mkdir(g_state.local_root);
    char artifacts[112], logs_dir[112];
    snprintf(artifacts, sizeof(artifacts), "%s/artifacts", g_state.local_root);
    snprintf(logs_dir, sizeof(logs_dir), "%s/logs", g_state.local_root);
    sd.mkdir(artifacts);
    sd.mkdir(logs_dir);

    // ── Copy artifacts from captureDir ───────────────────────────────────────
    char artBuf[160], srcBuf[160];
    bool hasPcap = false, hasHc = false, hasStats = false;

    if (p.capture_dir && p.capture_dir[0]) {
        // handshakes.pcap
        snprintf(srcBuf, sizeof(srcBuf), "%s/handshakes.pcap", p.capture_dir);
        snprintf(artBuf, sizeof(artBuf), "%s/handshakes.pcap", artifacts);
        if (safeCopy(sd, srcBuf, artBuf)) hasPcap = true;

        // raw.pcap
        snprintf(srcBuf, sizeof(srcBuf), "%s/raw.pcap", p.capture_dir);
        snprintf(artBuf, sizeof(artBuf), "%s/raw.pcap", artifacts);
        if (!hasPcap) hasPcap = safeCopy(sd, srcBuf, artBuf);
        else safeCopy(sd, srcBuf, artBuf);

        // beacon.pcap
        snprintf(srcBuf, sizeof(srcBuf), "%s/beacon.pcap", p.capture_dir);
        snprintf(artBuf, sizeof(artBuf), "%s/beacon.pcap", artifacts);
        safeCopy(sd, srcBuf, artBuf);

        // specter.hc22000
        snprintf(srcBuf, sizeof(srcBuf), "%s/specter.hc22000", p.capture_dir);
        snprintf(artBuf, sizeof(artBuf), "%s/specter.hc22000", artifacts);
        if (safeCopy(sd, srcBuf, artBuf)) hasHc = true;

        // specter.hc (alternate extension)
        if (!hasHc) {
            snprintf(srcBuf, sizeof(srcBuf), "%s/specter.hc", p.capture_dir);
            snprintf(artBuf, sizeof(artBuf), "%s/specter.hc22000", artifacts);
            if (safeCopy(sd, srcBuf, artBuf)) hasHc = true;
        }

        // stats.csv
        snprintf(srcBuf, sizeof(srcBuf), "%s/stats.csv", p.capture_dir);
        snprintf(artBuf, sizeof(artBuf), "%s/stats.csv", artifacts);
        if (safeCopy(sd, srcBuf, artBuf)) hasStats = true;

        // capture_summary.txt → logs
        snprintf(srcBuf, sizeof(srcBuf), "%s/capture_summary.txt", p.capture_dir);
        snprintf(artBuf, sizeof(artBuf), "%s/capture_summary.txt", logs_dir);
        safeCopy(sd, srcBuf, artBuf);

        // jammit log
        snprintf(srcBuf, sizeof(srcBuf), "%s/jammit_session.log", p.capture_dir);
        snprintf(artBuf, sizeof(artBuf), "%s/jammit_session.log", logs_dir);
        safeCopy(sd, srcBuf, artBuf);
    }

    // ── manifest.json ─────────────────────────────────────────────────────────
    const char* verdict = quickVerdict(p);
    String devId = deviceId();

    // Build findings array
    String findings = "";
    if (p.pmkid_count > 0) {
        findings += "{\"finding\":\"PMKID candidates observed\",\"count\":";
        findings += p.pmkid_count;
        findings += "},";
    }
    if (p.handshake_count > 0) {
        findings += "{\"finding\":\"EAPOL frames captured\",\"count\":";
        findings += p.handshake_count;
        findings += "},";
    }
    if (hasPcap)  findings += "{\"finding\":\"PCAP recorded\"},";
    if (hasHc)    findings += "{\"finding\":\"Hashcat export present\"},";
    if (hasStats) findings += "{\"finding\":\"Packet statistics logged\"},";
    // Remove trailing comma
    if (findings.endsWith(",")) findings.remove(findings.length() - 1);

    // Build artifacts array
    String artArray = "";
    if (hasPcap) artArray += "{\"type\":\"pcap\",\"path\":\"artifacts/handshakes.pcap\",\"required\":false},";
    if (hasHc)   artArray += "{\"type\":\"hash_export\",\"path\":\"artifacts/specter.hc22000\",\"format\":\"hc22000\",\"required\":false},";
    if (hasStats)artArray += "{\"type\":\"stats_csv\",\"path\":\"artifacts/stats.csv\",\"required\":false},";
    artArray += "{\"type\":\"events_csv\",\"path\":\"events.csv\",\"required\":true}";

    String manifest = "{";
    manifest += "\"schema\":\"neondrive.session.v1\",";
    manifest += "\"session_id\":\""; manifest += jsonEsc(g_state.session_id); manifest += "\",";
    manifest += "\"device\":{";
      manifest += "\"device_id\":\""; manifest += jsonEsc(devId.c_str()); manifest += "\",";
      manifest += "\"hardware\":\""; manifest += jsonEsc(ND_PROFILE_NAME); manifest += "\",";
      manifest += "\"profile_code\":\""; manifest += jsonEsc(boardCode()); manifest += "\"";
    manifest += "},";
    manifest += "\"profile\":{";
      manifest += "\"module\":\""; manifest += jsonEsc(mod); manifest += "\",";
      manifest += "\"mode\":\"controlled_lab_validation\"";
    manifest += "},";
    manifest += "\"target\":{";
      manifest += "\"ssid\":\""; manifest += jsonEsc(p.ssid ? p.ssid : ""); manifest += "\",";
      manifest += "\"bssid\":\""; manifest += jsonEsc(p.bssid ? p.bssid : ""); manifest += "\",";
      manifest += "\"channel\":"; manifest += p.channel; manifest += ",";
      manifest += "\"security\":\""; manifest += jsonEsc(p.security ? p.security : ""); manifest += "\"";
    manifest += "},";
    manifest += "\"timing\":{";
      manifest += "\"millis_since_boot\":"; manifest += (unsigned long)millis();
    manifest += "},";
    manifest += "\"artifacts\":["; manifest += artArray; manifest += "],";
    manifest += "\"quick_result\":{";
      manifest += "\"status\":\""; manifest += verdict; manifest += "\",";
      manifest += "\"findings\":["; manifest += findings; manifest += "]";
    manifest += "}}";

    char manifestPath[112];
    snprintf(manifestPath, sizeof(manifestPath), "%s/manifest.json", g_state.local_root);
    if (!writeFile(sd, manifestPath, manifest)) {
        strncpy(g_state.last_error, "Failed to write manifest.json", sizeof(g_state.last_error) - 1);
        strncpy(g_state.sync_status, "failed", sizeof(g_state.sync_status) - 1);
        return false;
    }

    // ── summary.json ─────────────────────────────────────────────────────────
    String summary = "{";
    summary += "\"schema\":\"neondrive.summary.v1\",";
    summary += "\"session_id\":\""; summary += jsonEsc(g_state.session_id); summary += "\",";
    summary += "\"device_id\":\""; summary += jsonEsc(devId.c_str()); summary += "\",";
    summary += "\"module\":\""; summary += jsonEsc(mod); summary += "\",";
    summary += "\"status\":\""; summary += verdict; summary += "\",";
    summary += "\"evidence\":{";
      summary += "\"pmkid_count\":"; summary += p.pmkid_count; summary += ",";
      summary += "\"handshake_frames\":"; summary += p.handshake_count; summary += ",";
      summary += "\"has_pcap\":"; summary += hasPcap ? "true" : "false"; summary += ",";
      summary += "\"has_hash_export\":"; summary += hasHc ? "true" : "false";
    summary += "},";
    summary += "\"target\":{";
      summary += "\"ssid\":\""; summary += jsonEsc(p.ssid ? p.ssid : ""); summary += "\",";
      summary += "\"bssid\":\""; summary += jsonEsc(p.bssid ? p.bssid : ""); summary += "\",";
      summary += "\"channel\":"; summary += p.channel;
    summary += "},";
    summary += "\"sync\":{";
      summary += "\"dropbox_status\":\"queued\",";
      summary += "\"analyzer_status\":\"not_seen\"";
    summary += "}}";

    char summaryPath[112];
    snprintf(summaryPath, sizeof(summaryPath), "%s/summary.json", g_state.local_root);
    writeFile(sd, summaryPath, summary);  // non-fatal if fails

    // ── events.csv ───────────────────────────────────────────────────────────
    String events = "timestamp_ms,event_type,module,severity,target_ssid,target_bssid,channel,message,artifact_path\n";
    events += "0,session_start,";
    events += mod; events += ",info,";
    events += jsonEsc(p.ssid ? p.ssid : ""); events += ",";
    events += jsonEsc(p.bssid ? p.bssid : ""); events += ",";
    events += p.channel; events += ",Session bundle staged,\n";
    if (p.pmkid_count > 0) {
        events += String(millis()); events += ",pmkid_captured,";
        events += mod; events += ",warning,";
        events += jsonEsc(p.ssid ? p.ssid : ""); events += ",";
        events += jsonEsc(p.bssid ? p.bssid : ""); events += ",";
        events += p.channel; events += ",";
        events += p.pmkid_count; events += " PMKID candidate(s) observed,artifacts/specter.hc22000\n";
    }
    if (p.handshake_count > 0) {
        events += String(millis()); events += ",eapol_frames,";
        events += mod; events += ",info,";
        events += jsonEsc(p.ssid ? p.ssid : ""); events += ",";
        events += jsonEsc(p.bssid ? p.bssid : ""); events += ",";
        events += p.channel; events += ",";
        events += p.handshake_count; events += " EAPOL frame(s) captured,";
        events += hasPcap ? "artifacts/handshakes.pcap" : ""; events += "\n";
    }
    events += String(millis()); events += ",session_end,";
    events += mod; events += ",info,";
    events += jsonEsc(p.ssid ? p.ssid : ""); events += ",";
    events += jsonEsc(p.bssid ? p.bssid : ""); events += ",";
    events += p.channel; events += ",Bundle ready for Wardrive Analyzer sync,\n";

    char eventsPath[112];
    snprintf(eventsPath, sizeof(eventsPath), "%s/events.csv", g_state.local_root);
    writeFile(sd, eventsPath, events);

    strncpy(g_state.sync_status, "staged", sizeof(g_state.sync_status) - 1);
    Serial.printf("[analyzer] staged: %s\n", g_state.local_root);
    return true;
}

bool uploadSession(fs::FS& sd,
                   const char* dropbox_token,
                   const String& dropbox_folder) {
    if (!dropbox_token || !dropbox_token[0]) {
        strncpy(g_state.last_error, "Dropbox token not configured", sizeof(g_state.last_error) - 1);
        strncpy(g_state.sync_status, "failed", sizeof(g_state.sync_status) - 1);
        return false;
    }
    if (g_state.session_id[0] == '\0') {
        strncpy(g_state.last_error, "No staged session. Run Stage first.", sizeof(g_state.last_error) - 1);
        strncpy(g_state.sync_status, "failed", sizeof(g_state.sync_status) - 1);
        return false;
    }

    strncpy(g_state.sync_status, "syncing", sizeof(g_state.sync_status) - 1);
    g_state.last_error[0] = '\0';
    g_state.files_uploaded = 0;
    g_state.files_total = 0;

    // Remote base: <dropbox_folder>/neondrive/incoming/<device_id>/sessions/<session_id>
    String devId = deviceId();
    String remoteBase = dropbox_folder;
    if (remoteBase.endsWith("/")) remoteBase.remove(remoteBase.length() - 1);
    remoteBase += "/neondrive/incoming/";
    remoteBase += devId;
    remoteBase += "/sessions/";
    remoteBase += String(g_state.session_id);

    // Files to upload in order. Required = flag upload failure blocks sync_complete.
    struct UploadSpec { const char* rel; bool required; };
    static const UploadSpec specs[] = {
        { "manifest.json",               true  },
        { "summary.json",                false },
        { "events.csv",                  false },
        { "artifacts/handshakes.pcap",   false },
        { "artifacts/raw.pcap",          false },
        { "artifacts/beacon.pcap",       false },
        { "artifacts/specter.hc22000",   false },
        { "artifacts/stats.csv",         false },
        { "logs/capture_summary.txt",    false },
        { "logs/jammit_session.log",     false },
    };

    bool anyRequired = false;
    bool allRequiredOk = true;

    for (const auto& spec : specs) {
        char localPath[128];
        snprintf(localPath, sizeof(localPath), "%s/%s", g_state.local_root, spec.rel);
        if (!sd.exists(localPath)) continue;

        g_state.files_total++;
        String remotePath = remoteBase + "/" + spec.rel;
        Serial.printf("[analyzer] upload %s -> %s\n", localPath, remotePath.c_str());

        auto res = DropboxClient::uploadFile(sd, localPath, dropbox_token, remotePath.c_str());
        if (res.ok) {
            g_state.files_uploaded++;
        } else {
            Serial.printf("[analyzer] upload FAILED: %s\n", res.error);
            if (spec.required) {
                allRequiredOk = false;
                snprintf(g_state.last_error, sizeof(g_state.last_error),
                         "Required upload failed: %s — %s", spec.rel, res.error);
            }
        }
        if (spec.required) anyRequired = true;
    }

    // Upload sync_complete.flag last — only if required files all succeeded.
    if (!anyRequired || allRequiredOk) {
        char flagPath[112];
        snprintf(flagPath, sizeof(flagPath), "%s/sync_complete.flag", g_state.local_root);
        writeFile(sd, flagPath, "1");  // ensure flag file exists locally
        String remoteFlagPath = remoteBase + "/sync_complete.flag";
        auto res = DropboxClient::uploadFile(sd, flagPath, dropbox_token, remoteFlagPath.c_str());
        if (res.ok) {
            strncpy(g_state.sync_status, "synced", sizeof(g_state.sync_status) - 1);
            g_state.files_uploaded++;
            g_state.files_total++;
            Serial.printf("[analyzer] sync complete: %s\n", g_state.session_id);
            return true;
        } else {
            snprintf(g_state.last_error, sizeof(g_state.last_error),
                     "sync_complete.flag upload failed: %s", res.error);
        }
    }

    strncpy(g_state.sync_status, "failed", sizeof(g_state.sync_status) - 1);
    return false;
}

String statusJson(bool dropbox_configured, const String& dropbox_folder) {
    String devId = deviceId();
    String out = "{";
    out += "\"ok\":true,";
    out += "\"dropbox_configured\":"; out += dropbox_configured ? "true" : "false"; out += ",";
    out += "\"dropbox_folder\":\""; out += jsonEsc(dropbox_folder.c_str()); out += "\",";
    out += "\"device_id\":\""; out += jsonEsc(devId.c_str()); out += "\",";
    out += "\"session_id\":\""; out += jsonEsc(g_state.session_id); out += "\",";
    out += "\"local_root\":\""; out += jsonEsc(g_state.local_root); out += "\",";
    out += "\"module\":\""; out += jsonEsc(g_state.module_name); out += "\",";
    out += "\"sync_status\":\""; out += jsonEsc(g_state.sync_status); out += "\",";
    out += "\"last_error\":\""; out += jsonEsc(g_state.last_error); out += "\",";
    out += "\"files_uploaded\":"; out += g_state.files_uploaded; out += ",";
    out += "\"files_total\":"; out += g_state.files_total;
    out += "}";
    return out;
}

String sessionsJson(fs::FS& sd) {
    String out = "[";
    bool first = true;
    if (sd.exists(kSessionsRoot)) {
        File dir = sd.open(kSessionsRoot);
        if (dir) {
            File entry = dir.openNextFile();
            while (entry) {
                if (entry.isDirectory()) {
                    if (!first) out += ",";
                    out += "\"";
                    out += jsonEsc(entry.name());
                    out += "\"";
                    first = false;
                }
                entry.close();
                entry = dir.openNextFile();
            }
            dir.close();
        }
    }
    out += "]";
    return out;
}

String sessionManifestJson(fs::FS& sd, const char* session_id) {
    if (!session_id || !session_id[0])
        return "{\"ok\":false,\"error\":\"No session_id\"}";
    // Reject path traversal
    if (strstr(session_id, "..") || strstr(session_id, "/"))
        return "{\"ok\":false,\"error\":\"Invalid session_id\"}";
    char path[128];
    snprintf(path, sizeof(path), "%s/%s/manifest.json", kSessionsRoot, session_id);
    String content = readFile(sd, path);
    if (content.isEmpty())
        return "{\"ok\":false,\"error\":\"manifest.json not found\"}";
    return content;
}

} // namespace AnalyzerSync
