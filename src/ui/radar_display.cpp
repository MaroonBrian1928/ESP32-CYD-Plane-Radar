#include "ui/radar_display.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/runway_overlay.h"

namespace fonts = lgfx::v1::fonts;

namespace ui {
namespace radar {

uint16_t kColorBackground = 0x0000;
uint16_t kColorGrid = 0x0320;
uint16_t kColorLabel = 0xFFFF;
uint16_t kColorCenter = 0xFFFF;
uint16_t kColorAircraft = 0x001F;
uint16_t kColorTrackVector = 0xFFFF;
uint16_t kColorTagType = 0x5DFF;
uint16_t kColorTagAltitude = 0xFFE0;
uint16_t kColorRunway = 0x4D5F;
uint16_t kColorRunwayLabel = 0x7DFF;
uint16_t kColorTrail = 0x39C7;

}  // namespace radar

namespace {

lgfx::LovyanGFX* s_draw = &tft;
LGFX_Sprite s_frame(&tft);
bool s_frame_ready = false;
// Cached static layer (rings/labels/runways/legend): rendered once per range
// change and memcpy'd into s_frame each frame so only aircraft are redrawn.
LGFX_Sprite s_grid(&tft);
bool s_grid_ready = false;
int s_grid_built_index = -1;

class DrawScope {
 public:
  explicit DrawScope(lgfx::LovyanGFX& gfx) : prev_(s_draw) { s_draw = &gfx; }
  ~DrawScope() { s_draw = prev_; }

 private:
  lgfx::LovyanGFX* prev_;
};

// On a paletted sprite the "color" passed to a draw call is a PALETTE INDEX, not
// an RGB value. So the kColor* the drawing code uses are indices here; the real
// RGB lives in the sprite palette (see applyFramePalette). Index 0 = background.
void initPalette() {
  radar::kColorBackground = 0;
  radar::kColorGrid = 1;
  radar::kColorLabel = 2;
  radar::kColorCenter = 2;  // white, same as label
  radar::kColorAircraft = 3;
  radar::kColorTrackVector = 4;
  radar::kColorTagType = 5;
  radar::kColorTagAltitude = 6;
  radar::kColorRunway = 7;
  radar::kColorRunwayLabel = 8;
  radar::kColorTrail = 9;
}

constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRadF = 0.01745329252f;

// Dead-reckon an aircraft's CURRENT position from its last reported fix: advance
// it along its ground track at its ground speed for the true age of that fix.
// The key is anchoring to the fix's real measurement time, not when we received
// it: the feed's positions arrive already a few seconds stale (seen_pos_s), so
// elapsed = seen_pos_s + time-since-fetch. With that, the prediction from a new
// fix lands right where the previous fix's prediction already was — successive
// predictions agree, so there's no per-fetch snap-back/correction. Capped so a
// stalled feed doesn't fling stale targets off-screen.
void predictedLatLon(const services::adsb::Aircraft& p, float* out_lat,
                     float* out_lon) {
  *out_lat = p.lat;
  *out_lon = p.lon;
  if (p.gs_knots <= 0.0f) {
    return;
  }
  float elapsed_s = p.seen_pos_s +
                    (millis() - services::adsb::lastUpdateMillis()) / 1000.0f;
  if (elapsed_s < 0.0f) {
    elapsed_s = 0.0f;
  }
  if (elapsed_s > config::kRadarExtrapMaxSec) {
    elapsed_s = config::kRadarExtrapMaxSec;
  }

  const float dist_km = p.gs_knots * 1.852f * (elapsed_s / 3600.0f);
  const float track_rad = p.track_deg * kDegToRadF;
  const float coslat = std::max(0.01f, cosf(p.lat * kDegToRadF));
  *out_lat = p.lat + (dist_km * cosf(track_rad)) / kKmPerDeg;
  *out_lon = p.lon + (dist_km * sinf(track_rad)) / (kKmPerDeg * coslat);
}

// Per-aircraft position easing, keyed by ICAO hex: a light low-pass on the
// displayed position that absorbs the small residual jitter (measurement noise
// in position/speed/track) left after the prediction above. The prediction does
// the heavy lifting — the ease just takes the edge off — so it stays subtle.
struct PosTrail {
  char hex[7];
  float lat;
  float lon;
  unsigned long seen_ms;
  bool active;
};
PosTrail s_trail[services::adsb::kMaxAircraft] = {};

// Position easing time constant (ms): the displayed position closes the gap to
// the predicted target with this exponential time constant. Using a *time*
// constant (rather than a fixed fraction per frame) keeps motion smooth even
// when frame durations vary — e.g. the frame that also runs an ADS-B fetch is
// much longer than a 20 ms animation frame, and a per-frame fraction would lurch
// on those. Kept short: the age-anchored prediction already tracks the true
// position, so this only needs to soften small residual corrections, not bridge
// a whole fetch interval (which would add visible lag).
constexpr float kPosEaseTauMs = 140.0f;
// Per-frame easing fraction, recomputed once per frame from the measured frame
// dt as 1 - exp(-dt/tau). Seeded to the steady ~20 ms-frame value.
float s_pos_ease_alpha = 0.15f;

// Recompute the easing fraction from the time since the last frame. Call once
// per rendered frame, before easing any aircraft.
void updatePosEaseAlpha() {
  static unsigned long s_last_ms = 0;
  const unsigned long now = millis();
  float dt_ms = (s_last_ms == 0) ? config::kRadarAnimFrameMs : (now - s_last_ms);
  s_last_ms = now;
  if (dt_ms < 1.0f) {
    dt_ms = 1.0f;
  }
  // Cap dt hard. The frame that runs the ADS-B fetch blocks for a few hundred ms
  // (the HTTPS round-trip), and that long dt would otherwise drive the easing
  // fraction to ~1 — snapping straight to the target on exactly the frame the
  // fresh fix's correction arrives (the residual "snap-back"). Capping near a
  // couple animation frames keeps each step a gentle fraction, so the correction
  // eases out over the following fast frames instead of jumping.
  constexpr float kMaxEaseDtMs = 50.0f;
  if (dt_ms > kMaxEaseDtMs) {
    dt_ms = kMaxEaseDtMs;
  }
  s_pos_ease_alpha = 1.0f - expf(-dt_ms / kPosEaseTauMs);
}

void easedPosition(const services::adsb::Aircraft& p, float* out_lat,
                   float* out_lon) {
  float tgt_lat = 0.0f;
  float tgt_lon = 0.0f;
  predictedLatLon(p, &tgt_lat, &tgt_lon);  // dead-reckon to now (age-anchored)

  if (p.hex[0] == '\0') {  // no id to track — can't ease, draw target directly
    *out_lat = tgt_lat;
    *out_lon = tgt_lon;
    return;
  }

  const unsigned long now = millis();
  PosTrail* match = nullptr;
  PosTrail* free_slot = nullptr;
  PosTrail* oldest = &s_trail[0];
  for (auto& t : s_trail) {
    if (t.active && strcmp(t.hex, p.hex) == 0) {
      match = &t;
      break;
    }
    if (!t.active && free_slot == nullptr) {
      free_slot = &t;
    }
    if (t.seen_ms < oldest->seen_ms) {
      oldest = &t;
    }
  }

  if (match != nullptr) {
    match->lat += (tgt_lat - match->lat) * s_pos_ease_alpha;
    match->lon += (tgt_lon - match->lon) * s_pos_ease_alpha;
    match->seen_ms = now;
    *out_lat = match->lat;
    *out_lon = match->lon;
    return;
  }

  PosTrail* slot = (free_slot != nullptr) ? free_slot : oldest;
  strncpy(slot->hex, p.hex, sizeof(slot->hex));
  slot->hex[sizeof(slot->hex) - 1] = '\0';
  slot->lat = tgt_lat;
  slot->lon = tgt_lon;
  slot->seen_ms = now;
  slot->active = true;
  *out_lat = tgt_lat;
  *out_lon = tgt_lon;
}

// --- Flight-trail history ---------------------------------------------------
// A SEPARATE, small pool: trails only draw when fewer than kTrailMaxAircraft are
// in range, so a handful of slots suffices. Keeping this pool small (not one per
// tracked aircraft) lets each trail be long without bloating static RAM — which
// matters because static RAM competes with the heap that TLS/sprites need.
// Stored as int16 centi-km offsets from the (fixed) radar center (±327 km, 10 m
// resolution), so 4 bytes/point and range-independent (reprojects on zoom).
constexpr int kTrailLen = 480;   // ~8 min of path at ~1 point/fetch
constexpr int kTrailSlots = 16;  // ≥ the at-most-9 trails ever drawn at once
constexpr float kTrailFixedScale = 100.0f;  // km → centi-km
// Drop a trail once its aircraft has been gone this long, so a vanished plane's
// path doesn't linger or bridge across a gap if it briefly drops from the feed.
// Sized to a few fetch cycles to tolerate normal feed flicker.
constexpr unsigned long kTrailExpiryMs = 5000;
struct TrailHist {
  char hex[7];
  bool active;
  unsigned long seen_ms;
  uint16_t head;
  uint16_t count;
  int16_t dx[kTrailLen];
  int16_t dy[kTrailLen];
};
TrailHist s_trailhist[kTrailSlots] = {};

void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* dist_km);

int16_t clampCentiKm(float km) {
  const float c = km * kTrailFixedScale;
  if (c > 32767.0f) return 32767;
  if (c < -32768.0f) return -32768;
  return static_cast<int16_t>(lroundf(c));
}

// Find (or claim, evicting the least-recently-seen) the trail slot for a hex.
TrailHist* trailHistFor(const char* hex) {
  if (hex[0] == '\0') {
    return nullptr;
  }
  const unsigned long now = millis();
  TrailHist* match = nullptr;
  TrailHist* free_slot = nullptr;
  TrailHist* oldest = &s_trailhist[0];
  for (auto& th : s_trailhist) {
    if (th.active && strcmp(th.hex, hex) == 0) {
      match = &th;
      break;
    }
    if (!th.active && free_slot == nullptr) {
      free_slot = &th;
    }
    if (th.seen_ms < oldest->seen_ms) {
      oldest = &th;
    }
  }
  if (match != nullptr) {
    match->seen_ms = now;
    return match;
  }
  TrailHist* slot = (free_slot != nullptr) ? free_slot : oldest;
  strncpy(slot->hex, hex, sizeof(slot->hex));
  slot->hex[sizeof(slot->hex) - 1] = '\0';
  slot->active = true;
  slot->seen_ms = now;
  slot->head = 0;
  slot->count = 0;
  return slot;
}

// Forget the path of any aircraft that's no longer being tracked: a slot whose
// seen_ms hasn't been refreshed (trailHistFor isn't called for planes absent
// from the current feed) is freed once it's older than the grace period.
void pruneStaleTrails(unsigned long now) {
  for (auto& th : s_trailhist) {
    if (th.active && now - th.seen_ms > kTrailExpiryMs) {
      th.active = false;
      th.count = 0;
      th.head = 0;
    }
  }
}

// Append a reported fix to the trail history (call once per ADS-B update).
void trailAppend(TrailHist* slot, float lat, float lon) {
  if (slot == nullptr) {
    return;
  }
  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);
  slot->dx[slot->head] = clampCentiKm(dx_km);
  slot->dy[slot->head] = clampCentiKm(dy_km);
  slot->head = (slot->head + 1) % kTrailLen;
  if (slot->count < kTrailLen) {
    ++slot->count;
  }
}

void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* dist_km) {
  // Equirectangular projection: a degree of longitude is only 111·cos(lat) km,
  // so scale east-west by cos(center latitude). Without this, E-W distances and
  // on-screen positions are inflated by 1/cos(lat) (≈1.4× at 45°N).
  const float coslat =
      cosf(static_cast<float>(services::location::lat()) * kDegToRadF);
  *dx_km =
      static_cast<float>(lon - services::location::lon()) * kKmPerDeg * coslat;
  *dy_km =
      static_cast<float>(lat - services::location::lat()) * kKmPerDeg;
  *dist_km = sqrtf((*dx_km) * (*dx_km) + (*dy_km) * (*dy_km));
}

/** Flat lat/lon as x/y: 1° ≈ 111 km, north = screen up. */
void latLonToScreen(float lat, float lon, int* out_x, int* out_y) {
  const float outer_km = radar::rangeCurrent().outer_km;
  const float px_per_km = static_cast<float>(radar::kGridOuterRadius) / outer_km;

  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);

  *out_x = radar::kCenterX + static_cast<int>(lroundf(dx_km * px_per_km));
  *out_y = radar::kCenterY - static_cast<int>(lroundf(dy_km * px_per_km));
}

int distSqFromCenter(int x, int y) {
  const int dx = x - radar::kCenterX;
  const int dy = y - radar::kCenterY;
  return dx * dx + dy * dy;
}

void clipPointToOuterRing(int x0, int y0, int* x1, int* y1) {
  const int max_r = radar::kGridOuterRadius;
  const int max_r_sq = max_r * max_r;
  if (distSqFromCenter(*x1, *y1) <= max_r_sq) {
    return;
  }

  const int dx = *x1 - x0;
  const int dy = *y1 - y0;
  float t = 1.0f;
  for (int step = 0; step < 20; ++step) {
    const int px = x0 + static_cast<int>(lroundf(dx * t));
    const int py = y0 + static_cast<int>(lroundf(dy * t));
    if (distSqFromCenter(px, py) <= max_r_sq) {
      *x1 = px;
      *y1 = py;
      return;
    }
    t -= 0.05f;
    if (t <= 0.0f) {
      *x1 = x0;
      *y1 = y0;
      return;
    }
  }
}

int speedLineLengthPx(float gs_knots) {
  if (gs_knots <= 0.0f) {
    return 0;
  }

  // Fixed screen scale: 60 s horizon at gs, not tied to current range zoom.
  constexpr float kKmPerKnotPerHorizon =
      1.852f * radar::kAircraftTrackHorizonSec / 3600.0f;
  const float px =
      gs_knots * kKmPerKnotPerHorizon * radar::kGridOuterRadius /
      radar::kAircraftTrackRefOuterKm * radar::kAircraftTrackLengthScale;

  const int len = static_cast<int>(px + 0.5f);
  if (len < radar::kAircraftSpeedLineMinPx) {
    return radar::kAircraftSpeedLineMinPx;
  }
  return len;
}

void noseTip(int cx, int cy, float heading_deg, int* tip_x, int* tip_y) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  *tip_x = cx + static_cast<int>(lroundf(sinf(rad) * radar::kAircraftNoseLenPx));
  *tip_y = cy - static_cast<int>(lroundf(cosf(rad) * radar::kAircraftNoseLenPx));
}

void drawHeadingTriangle(int cx, int cy, float heading_deg, uint16_t color) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  const float sin_h = sinf(rad);
  const float cos_h = cosf(rad);

  int tip_x = 0;
  int tip_y = 0;
  noseTip(cx, cy, heading_deg, &tip_x, &tip_y);

  const int base_x =
      cx - static_cast<int>(lroundf(sin_h * static_cast<float>(radar::kAircraftTailLenPx)));
  const int base_y =
      cy + static_cast<int>(lroundf(cos_h * static_cast<float>(radar::kAircraftTailLenPx)));

  const int wing_x = static_cast<int>(lroundf(cos_h * radar::kAircraftTailHalfPx));
  const int wing_y = static_cast<int>(lroundf(sin_h * radar::kAircraftTailHalfPx));

  s_draw->fillTriangle(tip_x, tip_y, base_x + wing_x, base_y + wing_y,
                       base_x - wing_x, base_y - wing_y, color);
}

void drawSpeedVector(int cx, int cy, float heading_deg, float track_deg,
                     float gs_knots, uint16_t color) {
  const int len = speedLineLengthPx(gs_knots);
  if (len <= 0) {
    return;
  }

  int tip_x = 0;
  int tip_y = 0;
  noseTip(cx, cy, heading_deg, &tip_x, &tip_y);

  constexpr float kDegToRad = 0.01745329252f;
  const float rad = track_deg * kDegToRad;
  int ex = tip_x + static_cast<int>(lroundf(sinf(rad) * len));
  int ey = tip_y - static_cast<int>(lroundf(cosf(rad) * len));
  clipPointToOuterRing(tip_x, tip_y, &ex, &ey);
  if (ex == tip_x && ey == tip_y) {
    return;
  }
  s_draw->drawLine(tip_x, tip_y, ex, ey, color);
}

void applyTagStyle() {
  // Compact 6×8 classic font for the dense 3-line aircraft tags.
  s_draw->setFont(&fonts::Font0);
  s_draw->setTextSize(1);
}

int measureTagBlockWidth(const services::adsb::Aircraft& plane,
                         const char* dist_str) {
  applyTagStyle();
  int max_w = 0;
  if (plane.callsign[0] != '\0') {
    const int w = s_draw->textWidth(plane.callsign);
    if (w > max_w) {
      max_w = w;
    }
  }
  if (plane.type[0] != '\0') {
    const int w = s_draw->textWidth(plane.type);
    if (w > max_w) {
      max_w = w;
    }
  }
  if (dist_str[0] != '\0') {
    const int w = s_draw->textWidth(dist_str);
    if (w > max_w) {
      max_w = w;
    }
  }
  return max_w;
}

void drawAircraftTag(int x, int y, const services::adsb::Aircraft& plane,
                     const char* dist_str) {
  applyTagStyle();

  const int line_h = s_draw->fontHeight();
  const int block_w = measureTagBlockWidth(plane, dist_str);
  const int block_h = line_h * 3;
  int ly = y - block_h / 2;

  const int symbol_half =
      radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx;
  // West (left): tag toward center on the right; east (right): tag on the left.
  const bool tag_on_right = x < radar::kCenterX;
  int anchor_x = 0;
  if (tag_on_right) {
    anchor_x = x + symbol_half + radar::kAircraftLabelGapPx;
    anchor_x = std::min(anchor_x, radar::kScreenWidth - block_w - 1);
    s_draw->setTextDatum(textdatum_t::top_left);
  } else {
    anchor_x = x - symbol_half - radar::kAircraftLabelGapPx;
    anchor_x = std::max(anchor_x, block_w + 1);
    s_draw->setTextDatum(textdatum_t::top_right);
  }
  ly = std::max(1, std::min(ly, radar::kScreenHeight - block_h - 1));

  if (plane.callsign[0] != '\0') {
    s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
    s_draw->drawString(plane.callsign, anchor_x, ly);
  }
  ly += line_h;

  if (plane.type[0] != '\0') {
    s_draw->setTextColor(radar::kColorTagType, radar::kColorBackground);
    s_draw->drawString(plane.type, anchor_x, ly);
  }
  ly += line_h;

  if (dist_str[0] != '\0') {
    s_draw->setTextColor(radar::kColorTagAltitude, radar::kColorBackground);
    s_draw->drawString(dist_str, anchor_x, ly);
  }
}

// Format a center-relative distance in the active units (e.g. "2.3mi").
void formatDistance(float dist_km, char* buf, size_t len) {
  if (radar::useMiles()) {
    snprintf(buf, len, "%.1fmi", dist_km / radar::kKmPerMile);
  } else {
    snprintf(buf, len, "%.1fkm", dist_km);
  }
}

struct AircraftDrawItem {
  size_t index = 0;
  int x = 0;
  int y = 0;
  int dist_sq = 0;
  float dist_km = 0.0f;
  TrailHist* trail = nullptr;
};

void sortDrawItemsFarFirst(AircraftDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const AircraftDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

// Draw an aircraft's recent path: connect its buffered fixes (reprojected to
// screen each frame so it follows range changes), then to the current symbol.
void drawTrail(const TrailHist* slot, int cur_x, int cur_y, uint16_t color) {
  if (slot == nullptr || slot->count < 1) {
    return;
  }
  const float px_per_km =
      static_cast<float>(radar::kGridOuterRadius) / radar::rangeCurrent().outer_km;
  int prev_x = 0;
  int prev_y = 0;
  bool have_prev = false;
  for (uint16_t k = 0; k < slot->count; ++k) {
    const uint16_t idx = (slot->head + kTrailLen - slot->count + k) % kTrailLen;
    const float dx_km = slot->dx[idx] / kTrailFixedScale;
    const float dy_km = slot->dy[idx] / kTrailFixedScale;
    const int x = radar::kCenterX + static_cast<int>(lroundf(dx_km * px_per_km));
    const int y = radar::kCenterY - static_cast<int>(lroundf(dy_km * px_per_km));
    if (have_prev) {
      s_draw->drawLine(prev_x, prev_y, x, y, color);
    }
    prev_x = x;
    prev_y = y;
    have_prev = true;
  }
  s_draw->drawLine(prev_x, prev_y, cur_x, cur_y, color);  // last fix → now
}


void drawAircraft() {
  static unsigned long s_trail_fix_ms = 0;

  updatePosEaseAlpha();  // frame-time-based easing fraction (smooth motion)

  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();

  // A brand-new ADS-B fix arrived → append each plane's reported position to its
  // trail history exactly once (not every animation frame).
  const unsigned long fix_ms = services::adsb::lastUpdateMillis();
  const bool new_fix = fix_ms != s_trail_fix_ms;
  s_trail_fix_ms = fix_ms;

  // Forget paths of aircraft that have dropped out of view/feed.
  pruneStaleTrails(millis());

  AircraftDrawItem items[services::adsb::kMaxAircraft];
  size_t draw_count = 0;

  for (size_t i = 0; i < n; ++i) {
    float lat = 0.0f;
    float lon = 0.0f;
    easedPosition(planes[i], &lat, &lon);  // dead-reckon + smoothed display pos
    TrailHist* slot = trailHistFor(planes[i].hex);
    if (new_fix) {
      trailAppend(slot, planes[i].lat, planes[i].lon);  // record real fix
    }

    float dx_km = 0.0f;
    float dy_km = 0.0f;
    float dist_km = 0.0f;
    offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);

    int x = 0;
    int y = 0;
    latLonToScreen(lat, lon, &x, &y);
    // Draw every aircraft at its real position; cull only those that fall off
    // the physical screen. (No fixed-radius rim dots — those showed a fake
    // distance and were just clutter.)
    if (x < 0 || x >= radar::kScreenWidth || y < 0 ||
        y >= radar::kScreenHeight) {
      continue;
    }
    items[draw_count].index = i;
    items[draw_count].x = x;
    items[draw_count].y = y;
    items[draw_count].dist_sq = distSqFromCenter(x, y);
    items[draw_count].dist_km = dist_km;
    items[draw_count].trail = slot;
    ++draw_count;
  }

  sortDrawItemsFarFirst(items, draw_count);

  // Trails sit behind the symbols, and only when the view isn't crowded.
  if (radar::showTrails() &&
      n < static_cast<size_t>(config::kTrailMaxAircraft)) {
    for (size_t d = 0; d < draw_count; ++d) {
      drawTrail(items[d].trail, items[d].x, items[d].y, radar::kColorTrail);
    }
  }

  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    const int x = items[d].x;
    const int y = items[d].y;
    drawSpeedVector(x, y, planes[i].nose_deg, planes[i].track_deg,
                    planes[i].gs_knots, radar::kColorTrackVector);
    if (planes[i].is_helicopter) {
      // Rotorcraft: a filled circle (no fixed nose direction like a plane).
      s_draw->fillCircle(x, y, radar::kHeliMarkerRadiusPx,
                         radar::kColorAircraft);
    } else {
      drawHeadingTriangle(x, y, planes[i].nose_deg, radar::kColorAircraft);
    }
  }
  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    char dist_str[12];
    formatDistance(items[d].dist_km, dist_str, sizeof(dist_str));
    drawAircraftTag(items[d].x, items[d].y, planes[i], dist_str);
  }
}

void applyCardinalStyle() {
  // 16 px classic font for the N/S/E/W bezel labels.
  s_draw->setFont(&fonts::Font2);
  s_draw->setTextSize(1);
}

void applyScaleStyle() {
  // 16 px classic font for the range label / legend.
  s_draw->setFont(&fonts::Font2);
  s_draw->setTextSize(1);
}

void drawCardinalLabel(const char* text, int x, int y, textdatum_t datum) {
  applyCardinalStyle();
  s_draw->setTextDatum(datum);
  s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

// Range label anchored at its top-right corner (x,y = top-right of the text).
void drawScaleLabelWithBackground(const char* text, int x, int y) {
  applyScaleStyle();
  s_draw->setTextDatum(textdatum_t::top_right);

  const int tw = s_draw->textWidth(text);
  const int th = s_draw->fontHeight();
  constexpr int kPadX = 3;
  constexpr int kPadY = 2;

  const int left = x - tw - kPadX;
  const int top = y - kPadY;

  s_draw->fillRect(left, top, tw + kPadX * 2, th + kPadY * 2,
                   radar::kColorBackground);
  s_draw->setTextColor(radar::kColorGrid, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawGridRing(int cx, int cy, int r, uint16_t color) {
  if (r <= 0) {
    return;
  }
  const int thickness =
      std::max(1, static_cast<int>(radar::kGridStrokeHalfWidth * 2.0f));
  for (int i = 0; i < thickness && r - i > 0; ++i) {
    s_draw->drawCircle(cx, cy, r - i, color);
  }
}

void drawRings(int cx, int cy, int outer_radius) {
  for (int i = 1; i <= radar::kRingCount; ++i) {
    const int r = (outer_radius * i) / radar::kRingCount;
    drawGridRing(cx, cy, r, radar::kColorGrid);
  }
}

void drawCrosshairs(int cx, int cy, int radius, uint16_t color) {
  // Solid (non-AA) lines — paletted sprite can't blend.
  s_draw->drawLine(cx, cy - radius, cx, cy + radius, color);
  s_draw->drawLine(cx - radius, cy, cx + radius, cy, color);
}

void drawCenterDot(int cx, int cy) {
  s_draw->fillCircle(cx, cy, radar::kCenterDotRadius, radar::kColorCenter);
}

void drawCardinalLabels() {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int right_edge = radar::kScreenWidth - 1;
  const int bottom_edge = radar::kScreenHeight - 1;

  drawCardinalLabel("N", cx, radar::kCardinalNorthOffsetY, textdatum_t::top_center);
  drawCardinalLabel("S", cx, bottom_edge + radar::kCardinalSouthOffsetY,
                    textdatum_t::bottom_center);
  drawCardinalLabel("W", 0, cy, textdatum_t::middle_left);
  drawCardinalLabel("E", right_edge, cy, textdatum_t::middle_right);
}

void drawScaleLabel(int cx, int cy, int outer_radius) {
  (void)cx;
  (void)cy;
  (void)outer_radius;
  char scale_label[12];
  radar::formatCurrentRing3Label(scale_label, sizeof(scale_label));
  // Top-right corner (the right side margin is dead space, and there it isn't
  // covered by aircraft symbols/tags like the east spoke was).
  constexpr int kCornerMarginX = 10;
  constexpr int kCornerMarginY = 8;
  drawScaleLabelWithBackground(scale_label,
                               radar::kScreenWidth - 1 - kCornerMarginX,
                               kCornerMarginY);
}

// Symbol key in the bottom-left dead space (outside the circular grid): a plane
// triangle and a helicopter circle so the two markers are distinguishable.
void drawLegend() {
  applyScaleStyle();
  s_draw->setTextDatum(textdatum_t::middle_left);
  const int x_sym = 12;
  const int x_txt = 22;
  const int y_plane = radar::kScreenHeight - 34;
  const int y_heli = radar::kScreenHeight - 14;

  drawHeadingTriangle(x_sym, y_plane, 0.0f, radar::kColorAircraft);
  s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
  s_draw->drawString("Plane", x_txt, y_plane);

  s_draw->fillCircle(x_sym, y_heli, radar::kHeliMarkerRadiusPx,
                     radar::kColorAircraft);
  s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
  s_draw->drawString("Heli", x_txt, y_heli);
}

template <typename Gfx>
void drawStaticGrid(Gfx& gfx) {
  const DrawScope scope(gfx);
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int grid_r = radar::kGridOuterRadius;

  gfx.fillScreen(radar::kColorBackground);
  drawRings(cx, cy, grid_r);
  drawCrosshairs(cx, cy, grid_r, radar::kColorGrid);
  initPalette();
  runway::drawLargeAirportRunways(gfx);
  drawCenterDot(cx, cy);
  drawCardinalLabels();
  drawScaleLabel(cx, cy, grid_r);
  drawLegend();
  gfx.setTextDatum(textdatum_t::top_left);
}

// Map each palette index (see initPalette) to its real RGB565 color. These are
// the same color565 values the working 8-bit build used, so the panel renders
// them identically. The frame and grid sprites get identical palettes so the
// indices memcpy'd from one render the same in the other.
void applyFramePalette(LGFX_Sprite& spr) {
  spr.setPaletteColor(0, tft.color565(radar::kBgR, radar::kBgG, radar::kBgB));
  spr.setPaletteColor(1,
                      tft.color565(radar::kGridR, radar::kGridG, radar::kGridB));
  spr.setPaletteColor(2, tft.color565(255, 255, 255));
  if (config::kDisplayRgbOrder) {  // BGR panels: swap R/B so red renders red
    spr.setPaletteColor(
        3, tft.color565(radar::kAircraftB, radar::kAircraftG, radar::kAircraftR));
  } else {
    spr.setPaletteColor(
        3, tft.color565(radar::kAircraftR, radar::kAircraftG, radar::kAircraftB));
  }
  spr.setPaletteColor(4,
                      tft.color565(radar::kTrackR, radar::kTrackG, radar::kTrackB));
  spr.setPaletteColor(
      5, tft.color565(radar::kTagTypeR, radar::kTagTypeG, radar::kTagTypeB));
  spr.setPaletteColor(
      6, tft.color565(radar::kTagAltR, radar::kTagAltG, radar::kTagAltB));
  spr.setPaletteColor(7,
                      tft.color565(radar::kRunwayR, radar::kRunwayG, radar::kRunwayB));
  spr.setPaletteColor(8, tft.color565(radar::kRunwayLabelR, radar::kRunwayLabelG,
                                      radar::kRunwayLabelB));
  spr.setPaletteColor(9, tft.color565(radar::kTrailColorR, radar::kTrailColorG,
                                      radar::kTrailColorB));
  spr.setPaletteColor(15, tft.color565(radar::kBgR, radar::kBgG, radar::kBgB));
}

bool ensureFrameSprite() {
  if (s_frame_ready) {
    return true;
  }
  // 4-bit paletted full-screen buffer = 320×240×½ ≈ 38 KB — half the RAM of an
  // 8-bit buffer (relieving the heap for Wi-Fi/TLS), exact colors, and faster to
  // push. Safe ONLY because the radar draws no anti-aliased primitives: those
  // blend colors absent from the palette and fault the paletted read path, so
  // all lines/circles/text are solid (see display_font.cpp and the *Line/Circle
  // calls — drawLine/fillCircle, never drawWideLine/fillSmoothCircle).
  s_frame.setColorDepth(lgfx::v1::color_depth_t::palette_4bit);
  if (!s_frame.createSprite(radar::kScreenWidth, radar::kScreenHeight)) {
    Serial.printf("radar: frame sprite alloc failed (free=%u, largest=%u)\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return false;
  }
  s_frame_ready = true;
  applyFramePalette(s_frame);

  // Second 38 KB buffer for the cached static layer. If it can't allocate we
  // fall back to redrawing the grid every frame (still correct, just slower).
  s_grid.setColorDepth(lgfx::v1::color_depth_t::palette_4bit);
  if (s_grid.createSprite(radar::kScreenWidth, radar::kScreenHeight)) {
    s_grid_ready = true;
    applyFramePalette(s_grid);  // identical palette to s_frame
  } else {
    Serial.println("radar: grid cache unavailable — redrawing grid each frame");
  }
  return true;
}

// Rebuild the cached static layer only when the range changes (rings' scale
// label + runway overlay positions depend on it).
void ensureGridLayer() {
  if (!s_grid_ready) {
    return;
  }
  const int idx = static_cast<int>(radar::rangeIndex());
  if (s_grid_built_index == idx) {
    return;
  }
  initPalette();
  drawStaticGrid(s_grid);  // opens its own DrawScope(s_grid)
  s_grid_built_index = idx;
}

// Dynamic corner overlays (NOT part of the cached grid): top-left = aircraft
// count + a feed-freshness dot; bottom-right = NTP clock. Drawn each frame.
void drawOverlays() {
  constexpr unsigned long kFeedStaleMs = 6000;  // no update for this long = stale
  constexpr unsigned long kFeedPulseMs = 250;   // brief enlarge after each update
  applyScaleStyle();  // Font2

  // Top-left: aircraft count + freshness dot.
  char buf[8];
  snprintf(buf, sizeof(buf), "%u",
           static_cast<unsigned>(services::adsb::aircraftCount()));
  s_draw->setTextDatum(textdatum_t::top_left);
  s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
  s_draw->drawString(buf, 4, 3);

  const int dot_x = 4 + s_draw->textWidth(buf) + 7;
  const int dot_y = 10;
  const unsigned long since = millis() - services::adsb::lastUpdateMillis();
  if (since > kFeedStaleMs) {
    s_draw->fillCircle(dot_x, dot_y, 3, radar::kColorAircraft);  // red = stale
  } else {
    const int r = (since < kFeedPulseMs) ? 5 : 3;  // green; pulse on update
    s_draw->fillCircle(dot_x, dot_y, r, radar::kColorGrid);
  }

  // Bottom-right: HH:MM once NTP has set the clock.
  struct tm tm_now;
  if (getLocalTime(&tm_now, 0) && tm_now.tm_year > 120) {
    char clk[6];
    snprintf(clk, sizeof(clk), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
    s_draw->setTextDatum(textdatum_t::bottom_right);
    s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
    s_draw->drawString(clk, radar::kScreenWidth - 4, radar::kScreenHeight - 3);
  }
}

// Off-screen frame composited in one pass, then blit via a single pushSprite
// (no on-screen erase/redraw gap). With the grid cache, the static layer is a
// fast memcpy and only the aircraft are drawn each frame.
void renderFrame() {
  initPalette();
  if (s_grid_ready) {
    ensureGridLayer();
    memcpy(s_frame.getBuffer(), s_grid.getBuffer(),
           static_cast<size_t>(radar::kScreenWidth) * radar::kScreenHeight / 2);
  } else {
    drawStaticGrid(s_frame);  // opens its own DrawScope(s_frame)
  }
  {
    const DrawScope scope(s_frame);
    drawAircraft();
    drawOverlays();
  }
  s_frame.pushSprite(0, 0);
  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace

void radarDisplayInit() {
  if (ensureFrameSprite()) {
    Serial.printf("radar: frame sprite reserved (%dx%d, free=%u)\n",
                  radar::kScreenWidth, radar::kScreenHeight, ESP.getFreeHeap());
  }
}

void radarDisplayDraw() {
  initPalette();
  s_grid_built_index = -1;  // force the cached static layer to rebuild

  if (ensureFrameSprite()) {
    renderFrame();
    return;
  }

  // Fallback when the sprite can't be allocated: draw straight to the panel.
  const DrawScope scope(tft);
  drawStaticGrid(tft);
  drawAircraft();
  tft.setTextDatum(textdatum_t::top_left);
}

void radarDisplayRefreshAircraft() {
  initPalette();

  if (ensureFrameSprite()) {
    renderFrame();
    return;
  }

  radarDisplayDraw();
}

}  // namespace ui
