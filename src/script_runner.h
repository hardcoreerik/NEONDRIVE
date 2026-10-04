#pragma once
// G6 — Lab procedure scripting (B5): a minimal line-based macro runner.
// Deliberately NOT a JS interpreter: classic ESP32 RAM is tight, and lab
// procedures need auditability, not a programming language.
//
// Scripts live on SD: /neondrive/scripts/*.ndm — one command per line,
// `#` comments, double-quoted args may contain spaces:
//
//   # Baseline survey — lab procedure LP-001
//   karma start 6 30
//   delay 35
//   karma stop
//   portal start "CorpNet" 6 120
//   delay 130
//   portal stop
//   stage
//   sync
//
// Every executed step is logged to the staged session's events.csv (when a
// session is staged) and to Serial, making runs auditable. The runner is
// non-blocking: ScriptRunner::tick() advances one step at a time from loop().
//
// Command dispatch (ScriptRunnerExec) is implemented by the application in
// main.cpp, where all module globals live.
#include <Arduino.h>
#include <FS.h>
#include <vector>

namespace ScriptRunner {

// Implemented by the application (main.cpp). Return true on success.
bool execCommand(const String& cmd, const std::vector<String>& args);

// Lifecycle
bool   runFile(fs::FS& sd, const char* path);  // start running a script
void   stop();                                  // abort the current run
void   tick();                                  // advance — call from loop()
bool   running();
String statusJson();                            // for the web UI

// List available scripts as JSON: [{"name":"...","size":123}, ...]
String listScripts(fs::FS& sd);

// Parse one line into command + args (handles "quoted args"). Returns false
// for blank/comment lines.
bool parseLine(const String& line, String& cmd, std::vector<String>& args);

} // namespace ScriptRunner
