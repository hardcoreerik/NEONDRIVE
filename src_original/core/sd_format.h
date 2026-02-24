// SD card formatting helpers
#pragma once

#include <Arduino.h>

namespace SDFormat {
    enum class FormatMode : uint8_t {
        QUICK,
        FULL
    };

    using ProgressCallback = void (*)(const char* stage, uint8_t percent);

    struct Result {
        bool success;
        bool usedFallback;
        char message[64];
    };

    // Attempts FAT32 format if possible; may fall back to wipe if allowFallback is true.
    Result formatCard(FormatMode mode, bool allowFallback, ProgressCallback cb = nullptr);
}
