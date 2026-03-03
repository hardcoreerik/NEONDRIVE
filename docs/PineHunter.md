# PineAP Hunter - WiFi Pineapple Detection System

## Overview
PineAP Hunter is a defensive security feature for M5Stick-NEMO that detects WiFi Pineapples by identifying Access Point BSSIDs (MAC addresses) that advertise multiple ESSIDs (network names). This is a common signature of the Hak5 WiFi Pineapple and similar attack platforms.

## ✅ Implementation Status: COMPLETE & ENHANCED
PineAP Hunter has been fully implemented and integrated into the M5Stick-NEMO codebase following established architectural patterns. Recent enhancements include improved navigation, RSSI display, and UI consistency.

## Architecture

### Core Detection Logic
- **Data Collection**: Continuously scan WiFi networks using ESP32's built-in `WiFi.scanNetworks()` function
- **Pattern Recognition**: Track BSSID → ESSID mappings and identify BSSIDs advertising multiple network names
- **Threshold-Based Alerting**: Alert when a BSSID exceeds the configured SSID count threshold
- **Memory Management**: Maintain a rolling buffer of the last 50 BSSID/SSID pairs to balance detection accuracy with memory usage

### Data Structures

```cpp
struct SSIDRecord {
  String essid;               // Network name
  int32_t rssi;              // Signal strength for this SSID

  SSIDRecord(const String& ssid, int32_t signal) : essid(ssid), rssi(signal) {}
};

struct PineRecord {
  uint8_t bssid[6];           // MAC address
  std::vector<SSIDRecord> essids;  // Associated network names with RSSI
  int32_t rssi;               // Signal strength of most recent scan
  uint32_t last_seen;         // Timestamp of last detection
};

struct PineAPHunterStats {
  std::vector<PineRecord> detected_pineaps;
  std::map<String, std::vector<SSIDRecord>> scan_buffer;  // BSSID -> SSIDs with RSSI
  uint32_t total_scans;
  uint32_t last_scan_time;
  bool list_changed;          // UI optimization flag
  int cursor_bssid_index;     // Navigation cursor
  int cursor_ssid_index;      // Navigation cursor
  int view_mode;              // 0=main list, 1=BSSID details, 2=SSID list
};
```

## User Interface Design

### Three-Level Navigation System

#### Level 1: Main PineAP Hunter Display
```
PineAP Hunter (inverse colors, medium font)
 33:aa:bb: 17
 99:88:77: 8
 dd:ee:ff: 6
 Back
```
- Shows detected PineAP devices with SSID counts (abbreviated BSSIDs)
- Scrolling support for long lists (similar to WiFi scan)
- Cursor navigation with "Next" button - always accessible
- "Select" button drills down to SSID list
- "Back" option always available and selectable

#### Level 2: BSSID Details (Future Enhancement)
```
00:11:22:33:aa:bb
Signal: -45 dBm
SSIDs: 17
Last Seen: 2s ago
Back
Sel: View SSIDs
```

#### Level 2: SSID List (Direct from Main List)
```
aa:bb:cc
 [-73] WiFiFoFum
 [-38] FlyForAWiFi
 [-45] FBISurveillanceVan
 [-52] StarbucksWiFi
 Back
```
- Shows all ESSIDs for selected BSSID with real-time RSSI values
- RSSI format: `[signal_strength] SSID_name`
- Scrollable list with cursor navigation (similar to WiFi scan)
- RSSI values update every second for real-time monitoring
- "Next" button for navigation
- "Back" returns to main list

## Settings Integration

### PH Alert SSIDs Setting
- **Location**: Settings Menu (Process 2), positioned under "DH Alert Pkts"
- **Default Value**: 5 SSIDs
- **Range**: 2-100 SSIDs
- **Behavior**: Wrap-around logic for increment/decrement (like existing settings)
- **Storage**: EEPROM byte reservation for persistence

### EEPROM Integration Points
1. **Range Check**: Add to validation logic near line 2453
2. **Default Values**: Add to EEPROM.write block for factory reset
3. **Load/Save**: Integrate with existing settings management functions

## Implementation Files

### pine_hunter.h
Header file containing:
- Data structure definitions (PineRecord, PineHunterStats)
- Global variable declarations
- Function declarations (following NEMO patterns)

### m5stick-nemo.ino
Main implementation containing:
- All Pine Hunter function implementations
- WiFi scanning and analysis logic
- UI rendering and navigation functions
- Memory management routines

### Key Functions Implemented
```cpp
void pine_hunter_setup();           // Initialize PineHunter mode
void pine_hunter_loop();            // Main operation loop
void scan_and_analyze();            // WiFi scanning and detection
void update_display();              // Smart UI updates
void add_scan_result();             // Memory management
void process_scan_results();        // Detection algorithm
bool is_pineapple_detected();       // Threshold checking
void handle_main_list_input();      // Input handling
void play_alert_beep();             // Audio feedback
void draw_main_list();              // UI rendering
void draw_ssid_list();              // SSID detail view
String bssid_to_string();           // Utility functions
void string_to_bssid();             // Utility functions
```

## Technical Specifications

### Memory Management
- **Buffer Size**: 50 BSSID/SSID pair records maximum
- **Overflow Handling**: FIFO (First In, First Out) replacement strategy
- **Memory Optimization**: String deduplication for common SSIDs

### Scanning Strategy
- **Method**: Use existing `WiFi.scanNetworks()` approach (proven reliable)
- **Frequency**: Continuous background scanning during active mode
- **Channel Coverage**: Automatic channel hopping handled by ESP32 WiFi stack

### UI Optimization Features
- **Smart Updates**: Only refresh display when detection results change
- **Cursor Persistence**: Maintain user's position in list during updates (Principle of Least Astonishment)
- **Audio Feedback**: Single beep when pineapples detected (count > 0), not spammy

### Performance Considerations
- **Scan Throttling**: Limit scan frequency to prevent performance degradation
- **Background Processing**: Non-blocking analysis to maintain UI responsiveness
- **Memory Monitoring**: Track usage to prevent heap fragmentation

## ✅ Integration Points - IMPLEMENTED

### Process Handler System
- **Process ID 26**: Pine Hunter main functionality ✅
- **Process ID 33**: PH Alert SSIDs setting ✅
- **Menu Integration**: Added to `wsmenu[]` array in WiFi tools menu ✅
- **Navigation**: Follows standard NEMO menu flow patterns ✅

### Settings Integration - COMPLETE
- **EEPROM Byte 10**: Reserved for threshold storage ✅
- **Range Validation**: Added to EEPROM bounds checking ✅
- **Default Value**: Set to 5 SSIDs ✅
- **Settings Menu**: "PH Alert SSIDs" added after "DH Alert Pkts" ✅
- **Wrap-around Logic**: Proper 2-100 range with overflow handling ✅

### Localization Support - COMPLETE
Text constants added to all supported languages:
```cpp
#define TXT_PINE_HUNTER "PineAP Hunter"     // Updated to PineAP Hunter
#define TXT_PH_ALERT_SSIDS "PH Alert SSIDs"
#define TXT_SEL_INFO "Sel: Info"
#define TXT_VIEW_SSIDS "Sel: View SSIDs"
```
Languages supported: EN_US, IT_IT, PT_BR, FR_FR ✅

## Testing Strategy

### Unit Testing
- Memory management under various load conditions
- Threshold boundary testing (edge cases at 2 and 100)
- EEPROM persistence across reboots

### Integration Testing
- Menu navigation flow validation
- Settings integration verification
- WiFi scanning accuracy in various environments

### User Experience Testing
- Cursor position maintenance during list updates
- Audio feedback appropriateness
- Performance under continuous operation

## Security Considerations

### Defensive Focus
PineHunter is designed as a **defensive security tool** to:
- Help users identify potential wireless attack platforms in their environment
- Provide early warning of suspicious WiFi activity
- Enable users to make informed decisions about network security

### Ethical Usage
- Detection only - no active interference with detected devices
- Educational value in understanding wireless security threats
- Promotes awareness of WiFi Pineapple attack vectors

## Future Enhancements

### Potential Features
- **Historical Logging**: Track detection events over time
- **Export Capability**: Save detection logs to SD card
- **Advanced Analytics**: Pattern recognition for known Pineapple signatures
- **Integration**: Coordinate with other NEMO defensive tools

### Performance Optimizations
- **Selective Scanning**: Focus on channels with suspicious activity
- **Machine Learning**: Improve detection accuracy with usage patterns
- **Network Mapping**: Visualize AP relationships and anomalies

## Developer Notes

### Code Style
- Follow existing NEMO patterns and conventions
- Maintain consistency with DeauthHunter and BLEHunter implementations
- Use established error handling and memory management practices

### Compilation Requirements
⚠️ **Important**: PineAP Hunter code should be compiled using the Arduino IDE, not command-line tools like arduino-cli. The complex template and STL usage may cause timeout or compilation issues with command-line compilation.

### Dependencies
- ESP32 WiFi libraries (already included)
- Standard C++ STL containers (vector, string)
- M5Stack display and input libraries
- NEMO's existing menu and settings systems

## ✅ Implementation Complete - Ready for Use!

### Recent Enhancements (2025)

**🎯 UI/UX Improvements:**
- **Fixed Navigation Issues**: "Back" option now always selectable and accessible
- **Improved Display**: Consistent scrolling behavior matching WiFi scan interface
- **Real-time RSSI**: Individual RSSI values per SSID with 1-second refresh rate
- **Better Spacing**: Proper line spacing and text formatting throughout

**🔧 Technical Improvements:**
- **Enhanced Data Structures**: Added `SSIDRecord` struct to store SSID + RSSI pairs
- **Faster Updates**: Display refresh every 1 second vs previous 2 seconds
- **Consistent Navigation**: Simplified cursor logic matching WiFi scan patterns
- **Memory Efficiency**: Proper RSSI tracking per SSID without averaging

### What Was Accomplished

**📁 Files Created/Updated:**
- `pineap_hunter.h` - Enhanced header with RSSI-aware data structures
- `PineHunter.md` - Updated documentation reflecting current implementation

**🔧 Files Modified:**
- `m5stick-nemo.ino` - Enhanced PineAP Hunter with RSSI display and improved navigation
- `localization.h` - Added text constants including PH Alert SSIDs setting

**⚙️ Features Implemented:**
1. **Configurable Detection Threshold** (2-100 SSIDs, default 5)
2. **Two-Level UI Navigation** (Main list → SSID details with RSSI → Back)
3. **Smart Memory Management** (50 BSSID buffer with FIFO cleanup)
4. **Audio Alerts** (Non-spammy beep every 5 seconds when threats detected)
5. **Real-time RSSI Display** (Individual signal strength per SSID, updated every second)
6. **Scrolling Interface** (Consistent with WiFi scan behavior)
7. **Enhanced Navigation** (Always accessible "Back" option)

### How to Use PineAP Hunter

**🎯 Setup Detection Threshold:**
1. Main Menu → Settings → "PH Alert SSIDs"
2. Use Next button to adjust (2-100 range)
3. Press Select to save to EEPROM

**🔍 Monitor for Pineapples:**
1. Main Menu → WiFi → "PineAP Hunter"
2. Device continuously scans every 3 seconds
3. Displays BSSIDs with suspicious SSID counts
4. Audio beep alerts when threats detected

**📱 Navigate Results:**
- **Next Button**: Navigate through detected BSSIDs and SSIDs
- **Select Button**: View individual SSIDs with RSSI for selected BSSID
- **Back Option**: Always available and selectable to return to previous level

**🚨 Understanding Results:**
- **Main List Format**: `33:aa:bb: 7` (abbreviated BSSID: SSID count)
- **SSID List Format**: `[-73] WiFiFoFum` (RSSI in dBm + SSID name)
- Counts ≥ threshold = Potential WiFi Pineapple
- RSSI values update in real-time every second
- Negative values are normal (closer to 0 = stronger signal)

### Architecture Patterns Followed
- **Header Structure**: Enhanced `pineap_hunter.h` with RSSI-aware data structures
- **Implementation Location**: Functions in main .ino file
- **Global Variables**: Proper extern declarations
- **Process Handler**: Standard NEMO process ID system (Process 26)
- **Menu Integration**: Follows established wsmenu patterns with proper case handling
- **EEPROM Management**: Consistent with other hunter settings (Process 33)
- **UI Consistency**: Navigation patterns matching WiFi scan interface

This implementation provides a sophisticated WiFi Pineapple detection system that seamlessly integrates with M5Stick-NEMO's defensive security toolkit while maintaining code consistency and user experience standards. The recent enhancements ensure real-time RSSI monitoring and intuitive navigation that matches the established NEMO interface patterns.