# CYDCompanion — Android Companion App

Android companion app for the [NEONDRIVE firmware](../README.md).
Connects to the ESP32 over WiFi (device AP or local network) and provides a
live console, packet telemetry viewer, AI-assisted target analysis, and
WiGLE / wpa.sec sync management.

**Theme:** Tokyo Neon — inky indigo base, neon cyan/magenta accents, animated grid horizon.

---

## Requirements

- Android **8.0 (API 26)** or higher
- NEONDRIVE firmware flashed to a supported CYD or LilyGO device
- Android Studio **Iguana (2023.2.1)** or later, **or** a JDK 17+ environment for command-line builds

---

## Build from Source

### Option A — Android Studio (recommended)

1. Open this folder (`CYDCompanion/`) in Android Studio
2. Let Gradle sync complete
3. Run on a physical device or emulator: **Run ▶ → app**

### Option B — Command line (Gradle)

```bash
# From the CYDCompanion/ directory:
./gradlew assembleDebug
```

Output APK: `app/build/outputs/apk/debug/app-debug.apk`

For a release build (requires signing config):
```bash
./gradlew assembleRelease
```

---

## Install via ADB (sideload)

After building, install directly to a connected device:

```bash
# Debug build
adb install app/build/outputs/apk/debug/app-debug.apk

# Or flash to a specific device if multiple are connected
adb -s <DEVICE_SERIAL> install app/build/outputs/apk/debug/app-debug.apk
```

To find connected device serials:
```bash
adb devices
```

Enable **Developer Options** and **USB Debugging** on the Android device first.
Settings → About Phone → tap Build Number 7 times → Developer Options → USB Debugging.

---

## Connecting to the Firmware

1. Flash NEONDRIVE to your CYD or LilyGO device
2. The device broadcasts a WiFi AP (`CYD-LIVE` by default) when not connected to a saved network
3. Connect your phone to that AP **or** connect both to the same LAN
4. Open CYDCompanion → Settings → set the CYD IP (default: `192.168.4.1` for AP mode)

---

## Project Structure

```
app/src/main/java/com/example/cydcompanion/
├── data/               # Repositories, settings store, BLE/WiFi transport
├── ui/                 # Compose screens (Live, Analyze, Control, Settings, AI)
└── MainActivity.kt
```

---

## Dependencies

| Library | Purpose |
|---|---|
| Jetpack Compose + Material3 | UI |
| Kotlin Coroutines / Flow | Async data streams |
| DataStore Preferences | Persistent settings |
| OkHttp / Ktor (see build.gradle) | HTTP transport to firmware web API |
| ArduinoJson-compatible JSON | Packet parsing |

See [`gradle/libs.versions.toml`](gradle/libs.versions.toml) for pinned versions.

---

## License

Same as the parent project — see [../LICENSE](../LICENSE).
