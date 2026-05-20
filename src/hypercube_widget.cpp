#include "hypercube_widget.h"
#include <math.h>

// ── Constants ────────────────────────────────────────────────────────────────

static constexpr float  PI2          = 6.28318530718f;
static constexpr int    SPRITE_W     = HypercubeWidget::REGION_W;
static constexpr int    SPRITE_H     = HypercubeWidget::REGION_H;
static constexpr uint32_t TICK_MS   = 33;   // ~30 fps
static constexpr float  ROT_SPEED_XY = 0.018f;
static constexpr float  ROT_SPEED_ZW = 0.011f;
static constexpr float  ROT_SPEED_XZ = 0.007f;

// 4D→3D and 3D→2D perspective eye distances
static constexpr float  D4    = 2.5f;
static constexpr float  D3    = 3.5f;
static constexpr float  SCALE = 10.5f;  // fits in 52-px sprite

// ── Module state ─────────────────────────────────────────────────────────────

static TFT_eSprite* spr      = nullptr;
static TFT_eSPI*    _tft     = nullptr;
static bool         enabled  = true;
static HypercubeWidget::Activity activity = HypercubeWidget::Activity::IDLE;

static float    angleXY    = 0.0f;
static float    angleZW    = 0.0f;
static float    angleXZ    = 0.0f;
static uint32_t lastTickMs = 0;
static int      sprX       = 0;
static int      sprY       = 0;

// ── Colour table ─────────────────────────────────────────────────────────────

static uint16_t edgeColour() {
  using A = HypercubeWidget::Activity;
  switch (activity) {
    case A::SCANNING:   return 0x07FF;   // cyan
    case A::CAPTURING:  return 0xFFE0;   // yellow
    case A::ATTACKING:  return 0xF800;   // red
    case A::GPS_ACTIVE: return 0x07E0;   // green
    case A::IDLE:
    default:            return 0x0455;   // dim cyan
  }
}

// ── 4-D types & helpers ───────────────────────────────────────────────────────

struct Vec4 { float x, y, z, w; };
struct Vec2 { float x, y; };

// Rotate two components of a Vec4 in-place.
// a, b are component indices: 0=x, 1=y, 2=z, 3=w
static void rot4(Vec4& v, int a, int b, float angle) {
  float* pa = &v.x + a;
  float* pb = &v.x + b;
  float ca = cosf(angle), sa = sinf(angle);
  float na = *pa * ca - *pb * sa;
  float nb = *pa * sa + *pb * ca;
  *pa = na;
  *pb = nb;
}

// 4D → 2D double-perspective projection.
static Vec2 project(const Vec4& v) {
  float w3 = 1.0f / (D4 - v.w);
  float x3 = v.x * w3, y3 = v.y * w3, z3 = v.z * w3;
  float w2 = 1.0f / (D3 - z3);
  return { x3 * SCALE * w2, y3 * SCALE * w2 };
}

// ── Render ────────────────────────────────────────────────────────────────────

static void renderSprite() {
  spr->fillSprite(TFT_BLACK);

  // Build & rotate all 16 tesseract vertices.
  // Vertex i has coordinate +1 where bit k of i is set, -1 otherwise.
  Vec4 verts[16];
  for (int i = 0; i < 16; i++) {
    verts[i] = {
      (i & 1) ? 1.0f : -1.0f,
      (i & 2) ? 1.0f : -1.0f,
      (i & 4) ? 1.0f : -1.0f,
      (i & 8) ? 1.0f : -1.0f,
    };
    rot4(verts[i], 0, 1, angleXY);
    rot4(verts[i], 2, 3, angleZW);
    rot4(verts[i], 0, 2, angleXZ);
  }

  // Project to 2D sprite-space.
  Vec2 pts[16];
  const float cx = SPRITE_W * 0.5f;
  const float cy = SPRITE_H * 0.5f;
  for (int i = 0; i < 16; i++) {
    pts[i] = project(verts[i]);
    pts[i].x += cx;
    pts[i].y += cy;
  }

  // Draw 32 edges: pairs of vertices that differ in exactly one bit.
  uint16_t col = edgeColour();
  for (int i = 0; i < 16; i++) {
    for (int j = i + 1; j < 16; j++) {
      if (__builtin_popcount((unsigned)(i ^ j)) == 1) {
        spr->drawLine(
          (int16_t)pts[i].x, (int16_t)pts[i].y,
          (int16_t)pts[j].x, (int16_t)pts[j].y,
          col);
      }
    }
  }
}

static void pushToScreen() {
  if (_tft && spr) spr->pushSprite(sprX, sprY);
}

// ── Public API ────────────────────────────────────────────────────────────────

namespace HypercubeWidget {

void begin(TFT_eSPI& tft) {
  _tft = &tft;
  sprX = tft.width()  - REGION_PAD - REGION_W;
  sprY = REGION_PAD;

  spr = new TFT_eSprite(&tft);
  spr->setColorDepth(16);
  spr->createSprite(SPRITE_W, SPRITE_H);
  spr->fillSprite(TFT_BLACK);

  lastTickMs = millis();
}

void tick() {
  if (!enabled || !spr) return;
  uint32_t now = millis();
  if (now - lastTickMs < TICK_MS) return;
  lastTickMs = now;

  angleXY = fmodf(angleXY + ROT_SPEED_XY, PI2);
  angleZW = fmodf(angleZW + ROT_SPEED_ZW, PI2);
  angleXZ = fmodf(angleXZ + ROT_SPEED_XZ, PI2);

  renderSprite();
  pushToScreen();
}

void notifyScreenDrawn() {
  if (!enabled || !spr) return;
  // Re-render with current angles (no advance) and push immediately.
  renderSprite();
  pushToScreen();
}

void setEnabled(bool en) {
  enabled = en;
  if (!en && _tft) {
    _tft->fillRect(sprX, sprY, REGION_W, REGION_H, TFT_BLACK);
  } else if (en && spr) {
    renderSprite();
    pushToScreen();
  }
}

bool isEnabled() { return enabled; }

void setActivity(Activity a) { activity = a; }
Activity getActivity() { return activity; }

} // namespace HypercubeWidget
