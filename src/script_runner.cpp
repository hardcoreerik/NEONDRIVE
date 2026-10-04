// G6 — Lab procedure script runner. Clean-room implementation.
#include "script_runner.h"
#include "analyzer_sync.h"
#include <SD.h>

namespace ScriptRunner {
namespace {

constexpr size_t kMaxScriptBytes = 8192;
constexpr uint8_t kMaxArgs = 8;

String   scriptText;          // whole script in RAM (≤8 KB)
uint16_t lineStart = 0;       // byte offset of current line
bool     isRunning = false;
uint32_t delayUntilMs = 0;
bool     inDelay = false;
char     scriptName[48] = {0};
uint16_t stepCount = 0;
char     lastError[96] = {0};

void setErr(const char* e) {
    strncpy(lastError, e, sizeof(lastError) - 1);
    lastError[sizeof(lastError) - 1] = '\0';
}

void logStep(const String& line, bool ok, const char* note = "") {
    char msg[160];
    if (note && note[0])
        snprintf(msg, sizeof(msg), "script '%s' step %u: [%s] %s",
                 scriptName, stepCount, ok ? "OK" : "FAIL", note);
    else
        snprintf(msg, sizeof(msg), "script '%s' step %u: [%s] %s",
                 scriptName, stepCount, ok ? "OK" : "FAIL", line.c_str());
    Serial.printf("[script] %s\n", msg);
    // Audit trail — no-op when no session is staged.
    AnalyzerSync::appendEvent(SD, ok ? "script_step" : "script_step_failed",
                              "SCRIPT", ok ? "info" : "warning", msg);
}

// Fetch the next non-blank, non-comment line. Returns false at EOF.
bool nextLine(String& out) {
    while (lineStart < scriptText.length()) {
        int nl = scriptText.indexOf('\n', lineStart);
        uint16_t end = (nl < 0) ? scriptText.length() : (uint16_t)nl;
        out = scriptText.substring(lineStart, end);
        lineStart = end + 1;
        out.trim();
        if (out.length() == 0 || out[0] == '#') continue;
        // Strip trailing \r
        if (out.endsWith("\r")) out.remove(out.length() - 1);
        if (out.length() == 0 || out[0] == '#') continue;
        return true;
    }
    return false;
}

} // namespace

bool parseLine(const String& line, String& cmd, std::vector<String>& args) {
    cmd = "";
    args.clear();
    uint16_t i = 0, n = line.length();
    auto skipWs = [&]() { while (i < n && (line[i] == ' ' || line[i] == '\t')) i++; };
    auto readToken = [&](String& tok) {
        tok = "";
        skipWs();
        if (i >= n) return false;
        if (line[i] == '"') {
            i++;
            while (i < n && line[i] != '"') tok += line[i++];
            if (i < n) i++;  // closing quote
        } else {
            while (i < n && line[i] != ' ' && line[i] != '\t') tok += line[i++];
        }
        return true;
    };
    String tok;
    if (!readToken(tok)) return false;
    cmd = tok;
    cmd.toLowerCase();
    while (readToken(tok) && args.size() < kMaxArgs) args.push_back(tok);
    return true;
}

bool runFile(fs::FS& sd, const char* path) {
    if (isRunning) { setErr("a script is already running"); return false; }
    if (!path || !path[0]) { setErr("no path"); return false; }
    // Constrain to the scripts directory (no traversal).
    String p = path;
    if (p.indexOf("..") >= 0) { setErr("invalid path"); return false; }
    if (!p.startsWith("/neondrive/scripts/"))
        p = "/neondrive/scripts/" + p;

    File f = sd.open(p, FILE_READ);
    if (!f || f.isDirectory()) {
        if (f) f.close();
        setErr("cannot open script");
        return false;
    }
    if (f.size() > kMaxScriptBytes) { f.close(); setErr("script too large"); return false; }
    scriptText = f.readString();
    f.close();

    lineStart = 0;
    stepCount = 0;
    inDelay = false;
    delayUntilMs = 0;
    lastError[0] = '\0';
    strncpy(scriptName, p.c_str(), sizeof(scriptName) - 1);
    // Keep just the basename for logs.
    const char* slash = strrchr(scriptName, '/');
    if (slash) memmove(scriptName, slash + 1, strlen(slash));
    isRunning = true;
    Serial.printf("[script] started '%s' (%u bytes)\n", scriptName, scriptText.length());
    return true;
}

void stop() {
    if (!isRunning) return;
    isRunning = false;
    inDelay = false;
    scriptText = "";
    Serial.printf("[script] aborted '%s' after %u steps\n", scriptName, stepCount);
}

void tick() {
    if (!isRunning) return;
    uint32_t now = millis();
    if (inDelay) {
        if ((int32_t)(now - delayUntilMs) < 0) return;  // still waiting
        inDelay = false;
        logStep("delay", true, "wait complete");
    }
    String line;
    if (!nextLine(line)) {
        char msg[96];
        snprintf(msg, sizeof(msg), "script '%s' finished: %u steps",
                 scriptName, stepCount);
        Serial.printf("[script] %s\n", msg);
        AnalyzerSync::appendEvent(SD, "script_finished", "SCRIPT", "info", msg);
        isRunning = false;
        scriptText = "";
        return;
    }
    String cmd;
    std::vector<String> args;
    if (!parseLine(line, cmd, args)) return;  // shouldn't happen post-filter

    // `delay` is handled by the runner itself (non-blocking).
    if (cmd == "delay") {
        uint32_t secs = args.size() > 0 ? (uint32_t)args[0].toInt() : 1;
        if (secs > 3600) secs = 3600;
        inDelay = true;
        delayUntilMs = now + secs * 1000UL;
        stepCount++;
        char note[64];
        snprintf(note, sizeof(note), "waiting %lus", (unsigned long)secs);
        logStep(line, true, note);
        return;
    }

    stepCount++;
    bool ok = execCommand(cmd, args);
    logStep(line, ok, ok ? "" : lastError);
    if (!ok) {
        // Abort on first failure — lab procedures must not silently skip.
        char msg[128];
        snprintf(msg, sizeof(msg), "script '%s' aborted at step %u: %s",
                 scriptName, stepCount, lastError);
        Serial.printf("[script] %s\n", msg);
        AnalyzerSync::appendEvent(SD, "script_aborted", "SCRIPT", "warning", msg);
        isRunning = false;
        inDelay = false;
        scriptText = "";
    }
}

bool running() { return isRunning; }

String statusJson() {
    String out = "{\"ok\":true,\"running\":";
    out += isRunning ? "true" : "false";
    out += ",\"script\":\""; out += scriptName; out += "\"";
    out += ",\"steps\":"; out += stepCount;
    if (lastError[0]) { out += ",\"last_error\":\""; out += lastError; out += "\""; }
    out += "}";
    return out;
}

String listScripts(fs::FS& sd) {
    String out = "[";
    bool first = true;
    if (sd.exists("/neondrive/scripts")) {
        File dir = sd.open("/neondrive/scripts");
        if (dir) {
            File e = dir.openNextFile();
            while (e) {
                String name = e.name();
                if (!e.isDirectory() && name.endsWith(".ndm")) {
                    if (!first) out += ",";
                    out += "{\"name\":\""; out += name; out += "\",\"size\":";
                    out += (unsigned long)e.size(); out += "}";
                    first = false;
                }
                e.close();
                e = dir.openNextFile();
            }
            dir.close();
        }
    }
    out += "]";
    return out;
}

} // namespace ScriptRunner
