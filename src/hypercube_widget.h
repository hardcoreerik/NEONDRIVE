#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

// ──────────────────────────────────────────────────────────────────────────────
// HypercubeWidget — persistent animated 4D wireframe in the top-right corner.
//
// Lifecycle:
//   HypercubeWidget::begin(tft)        — call once in setup()
//   HypercubeWidget::tick()            — call every loop() tick
//   HypercubeWidget::notifyScreenDrawn() — call after every full screen redraw
//                                         so the sprite re-stamps itself
//
// The widget reserves a fixed pixel region. All UI layout code must avoid it.
// The region is exposed via REGION_* constants so screens can query it.
//
// Activity colours:
//   IDLE       — dim cyan
//   SCANNING   — cyan
//   CAPTURING  — yellow
//   ATTACKING  — red
//   GPS_ACTIVE — green
// ──────────────────────────────────────────────────────────────────────────────

namespace HypercubeWidget {

// Pixel dimensions of the reserved corner region.
// layoutActionDockBox() is wired to respect these.
static constexpr int REGION_W    = 52;
static constexpr int REGION_H    = 52;
static constexpr int REGION_PAD  = 4;   // gap from screen right/top edge

enum class Activity : uint8_t {
  IDLE,
  SCANNING,
  CAPTURING,
  ATTACKING,
  GPS_ACTIVE,
};

// Call once in setup() — creates the sprite and seeds rotation state.
void begin(TFT_eSPI& tft);

// Call every loop() — advances rotation and pushes sprite at ~30fps.
void tick();

// Call at the end of every full screen redraw (inside setScreen after draw).
// Immediately re-stamps the sprite so the cube is never buried.
void notifyScreenDrawn();

// Enable / disable the widget at runtime (persisted via AppConfig).
void setEnabled(bool en);
bool isEnabled();

// Signal what the device is currently doing — changes edge colour.
void setActivity(Activity a);
Activity getActivity();

} // namespace HypercubeWidget
