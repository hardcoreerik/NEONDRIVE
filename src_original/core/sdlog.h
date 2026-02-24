// SD Card Logger
#pragma once

#include <Arduino.h>

class SDLog {
public:
    static void init();
    static void setEnabled(bool enabled);
    static bool isEnabled() { return logEnabled; }
    
    // Log functions - mirror Serial.printf behavior
    static void log(const char* tag, const char* format, ...);
    static void logRaw(const char* message);
    
    // Flush to ensure data is written
    static void flush();
    
    // Close current log file (call on shutdown)
    static void close();
    
private:
    static bool logEnabled;
    static bool initialized;
    static char currentLogFile[64];
    
    static void ensureLogFile();
};

// Convenience macro - logs to both Serial and SD if enabled
// Always calls log() which does its own enabled check (more reliable across compilation units)
#define SDLOG(tag, fmt, ...) do { \
    Serial.printf("[%s] " fmt "\n", tag, ##__VA_ARGS__); \
    SDLog::log(tag, fmt, ##__VA_ARGS__); \
} while(0)
