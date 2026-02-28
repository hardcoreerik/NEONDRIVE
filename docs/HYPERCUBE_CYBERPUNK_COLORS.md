# Hypercube Cyber Punky Color System

## Overview
The hypercube widget in the top-right corner now reacts dynamically to **Just Go** automation stages, creating a visually striking "cyber punk" aesthetic that tells you exactly what the device is doing at a glance.

## Color Stages

### 🟢 SCAN Stage
**Purpose:** WiFi network scanning for targets  
**Colors:** Neon green + Cyan (alternating pulse)
- Primary: `0x07E0` (Acid Lime)
- Secondary: `0x07FF` (Bright Cyan)
- Accent: `0x9FF0` / `0x87FF` (Cyan variants)
- **Pulse Speed:** 120ms/cycle
- **Visual Feel:** Fast, searching energy - like sonar sweeping

### 💜 SELECT_TARGET Stage
**Purpose:** Targeting active network  
**Colors:** Electric blue + Purple (target lock pulse)
- Primary: `0x0C1F` (Deep Electric Blue)
- Secondary: `0x8018` (Purple)
- Accent: `0xF81F` (Hot Magenta)
- **Pulse Speed:** 150ms/cycle
- **Visual Feel:** Precision targeting - locking onto frequency

### 🔥 RUN_MODE Stage  (MAXIMUM CYBER PUNK!)
**Purpose:** Active attack execution (Y0INK, JAMMIT, RAW)
- **Color Set A:** Hot Magenta + Acid Lime + White
  - Primary: `0xF81F` (Hot Magenta/Neon Pink)
  - Secondary: `0x07E0` (Acid Lime)
  - Accent: `0xFFFF` (Pure White)
  
- **Color Set B:** Hot Magenta + Yellow + Cyan (Psychedelic!)
  - Primary: `0xF81F` (Hot Magenta)
  - Secondary: `0xFFE0` (Bright Yellow)
  - Accent: `0x07FF` (Cyan)
  
- **Pulse Speed:** 100ms/cycle (fastest for maximum intensity)
- **Visual Feel:** AGGRESSIVE, active warfare - full sensory overload

### 🔵 COOLDOWN Stage
**Purpose:** Attack finished, preparing next target
- **Color Set A:** Cyan + Deep Blue gradient
  - Primary: `0x07FF` (Bright Cyan)
  - Secondary: `0x0C1F` (Deep Blue)
  - Accent: `0x1099` (Dark Blue)
  
- **Color Set B:** Light Blue + Navy gradient
  - Primary: `0x067F` (Light Blue)
  - Secondary: `0x001F` (Navy)
  - Accent: `0x041F` (Darker Navy)
  
- **Pulse Speed:** 150ms/cycle
- **Visual Feel:** Calm, respite - system cooling down between targets

## Implementation Details

### Technical Architecture
- **Location:** `src/main.cpp` function `getHypercubePalette()`
- **State Tracking:** Uses `justGoActive` boolean + `justGoStage` enum
- **Color Source:** Each stage returns `HypercubePalette{edgeA, edgeB, nodeColor}`
- **Rendering:** Applied in `drawHypercubeWidget()` every frame
- **Pulse Mechanism:** Time-based modulo (`now / pulseMs & 0x01`)

### Priority Order (Checked in Order)
1. **Alert Flash** (`hypercubeFlashUntilMs` > 0): Orange ↔ Purple override
2. **Just Go Active:** Stage-specific cyber colors (THIS IS NEW!)
3. **Target Details + Channel Lock:** Red + light yellow + white
4. **WiFi Connected:** Green + cyan + white
5. **Default:** Cyan + magenta + green

### Color Values Quick Reference
```cpp
// Neon Colors
#define NEON_PINK     0xF81F    // Hot magenta
#define NEON_GREEN    0x07E0    // Acid lime
#define NEON_CYAN     0x07FF    // Bright cyan
#define ELECTRIC_BLUE 0x0C1F    // Deep electric blue
#define DARK_BLUE     0x001F    // Navy
#define PURPLE        0x8018    // Purple
#define YELLOW        0xFFE0    // Bright yellow
#define WHITE         0xFFFF
#define BLACK         0x0000
```

## User Experience

### What You'll See
1. **Boot Up:** Default cyan/magenta/green hypercube
2. **Just Go Activated:** Hypercube immediately shifts to **neon green + cyan** (SCAN stage)
3. **Target Selected:** Shifts to **deep blue + purple** (SELECT_TARGET stage)
4. **Attack Running:** Explodes into **HOT MAGENTA + ACID LIME** pulsing (RUN_MODE stage)
   - This is the most visually aggressive - tells you attacks are LIVE
5. **Between Targets:** Calms to **cyan + deep blue** (COOLDOWN stage)

### Color Psychology Behind Design
- **Green + Cyan (SCAN):** "Searching" - like radar/sonar sweeping
- **Blue + Purple (SELECT_TARGET):** "Precision" - locked and targeting
- **Magenta + Lime (RUN_MODE):** "ACTIVE ASSAULT" - maximum aggression and intensity
- **Deep Blue (COOLDOWN):** "Rest" - calm between actions

This creates a **narrative UI** where the hypercube tells the story of the attack in real-time through color!

## Testing Notes

**Tested With:**
- Real WiFi networks (hardcorewifi, otherwifi)
- Active deauth attacks (46+ deauth frames recorded)
- Handshake captures (1HS confirmed)
- Full 3-target automation cycle

**Verified Behaviors:**
- ✅ Colors transition smoothly between stages
- ✅ Pulse speeds differ per stage for visual distinction
- ✅ Hypercube remains visible at all times (not blocked by content)
- ✅ Pattern holds while scrolling/interacting with other UI
- ✅ Serial telemetry aligns with color stage changes

## Future Enhancements

Possible additions:
- **Brightness variation** based on packet count/activity level
- **Color intensity** increase as duration increases per stage
- **Multi-color gradient** edges instead of alternating
- **Per-mode colors** (Y0INK = green, JAMMIT = red, RAW = blue)
- **Attack success feedback** (color flash on handshake capture)
- **Saturation pulsing** for time-based phase indication

## Code Location
**Main Implementation:** `src/main.cpp` lines 2368-2411  
**Function:** `static HypercubePalette getHypercubePalette()`

---

**Created:** February 23, 2026  
**Device:** CYD (M5Stack Core Y with Pen)  
**Firmware Version:** NEONDRIVE // CYD  
**Status:** ✅ Active and tested
