#include "ui/radar_autozoom.h"

#include <Arduino.h>

#include <cmath>

#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"

namespace ui::radar {

namespace {

/** Dwell at each level while zooming in. */
constexpr unsigned long kStepMs = 4000;
/** While parked on the detail view, how often to re-check that the plane is
 *  still in it. The view is kept as long as a plane remains — this is just the
 *  poll cadence, not a countdown to zoom back out. */
constexpr unsigned long kHoldRecheckMs = 4000;

constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRadF = 0.01745329252f;

unsigned long s_next_ms = 0;

// Aircraft within `km` of the radar center (equirectangular distance, matching
// how the display projects positions). The feed is fetched to the widest preset
// while auto-zoom is on, so this sees every aircraft regardless of zoom.
int planesWithinKm(float km) {
  const size_t n = services::adsb::aircraftCount();
  if (n == 0) {
    return 0;
  }
  const services::adsb::Aircraft* ac = services::adsb::aircraftList();
  const float lat0 = static_cast<float>(services::location::lat());
  const float lon0 = static_cast<float>(services::location::lon());
  const float coslat = cosf(lat0 * kDegToRadF);
  const float r2 = km * km;
  int count = 0;
  for (size_t i = 0; i < n; ++i) {
    const float dx = (ac[i].lon - lon0) * kKmPerDeg * coslat;
    const float dy = (ac[i].lat - lat0) * kKmPerDeg;
    if (dx * dx + dy * dy <= r2) {
      ++count;
    }
  }
  return count;
}

// Tightest preset (smallest index) that still contains >=1 aircraft, or -1 if
// none are in range at any level. Counts are monotonic in range, so the first
// match scanning from the tightest preset outward is the answer.
int tightestWithPlane() {
  for (size_t i = 0; i < kRangePresetCount; ++i) {
    if (planesWithinKm(kRangePresets[i].outer_km) >= 1) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace

void autoZoomReset() {
  s_next_ms = 0;
  // Begin the tour from the widest level so it visibly walks inward through the
  // zoom levels (only when auto-zoom is on — don't clobber a manual preset).
  if (autoZoom()) {
    rangeSetIndex(static_cast<uint8_t>(kRangePresetCount - 1));
  }
}

bool autoZoomTick() {
  if (!autoZoom()) {
    return false;
  }
  const unsigned long now = millis();
  if (s_next_ms != 0 && now < s_next_ms) {
    return false;
  }

  const uint8_t max_idx = static_cast<uint8_t>(kRangePresetCount - 1);
  const uint8_t cur = rangeIndex();
  const int tightest = tightestWithPlane();

  uint8_t target = cur;
  unsigned long dwell = kStepMs;

  if (tightest < 0) {
    target = max_idx;  // nothing in range anywhere — sit wide and scan
  } else if (cur > static_cast<uint8_t>(tightest)) {
    target = static_cast<uint8_t>(cur - 1);  // zoom in one level toward detail
  } else if (cur < static_cast<uint8_t>(tightest)) {
    // The detail view emptied (its plane drifted outward) — follow it back out
    // to the tightest level that still holds it, rather than re-touring from max.
    target = static_cast<uint8_t>(tightest);
  } else {
    // Parked on the most-detailed level that still holds a plane. Stay put as
    // long as that's true; just re-check periodically (no forced zoom-out).
    target = cur;
    dwell = kHoldRecheckMs;
  }

  const bool changed = target != cur;
  if (changed) {
    rangeSetIndex(target);
  }
  s_next_ms = now + dwell;
  return changed;
}

}  // namespace ui::radar
