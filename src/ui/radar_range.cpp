#include "ui/radar_range.h"

#include "ui/radar_theme.h"

#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kPrefsRangeKey[] = "rangeIdx";
constexpr char kPrefsMilesKey[] = "useMiles";
constexpr char kPrefsRunwaysKey[] = "showRwys";
constexpr char kPrefsTrailsKey[] = "showTrls";
constexpr char kPrefsAutoZoomKey[] = "autoZoom";
constexpr uint8_t kDefaultRangeIndex = 1;  // 2 mi ring
// kKmPerMile now lives in radar_range.h (shared with the presets).

Preferences s_prefs;
uint8_t s_range_index = kDefaultRangeIndex;
bool s_use_miles = true;  // presets are mile-based; default to miles labels
bool s_show_runways = true;
bool s_show_trails = true;
bool s_auto_zoom = false;

void saveRangeIndex() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putUChar(kPrefsRangeKey, s_range_index);
  s_prefs.end();
}

void saveUseMiles() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsMilesKey, s_use_miles);
  s_prefs.end();
}

void saveShowRunways() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsRunwaysKey, s_show_runways);
  s_prefs.end();
}

void saveShowTrails() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsTrailsKey, s_show_trails);
  s_prefs.end();
}

void saveAutoZoom() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsAutoZoomKey, s_auto_zoom);
  s_prefs.end();
}

bool portalCheckboxChecked(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  // WiFiManager checkbox submits its value= attribute ("T", or "F" if we prefilled F).
  if ((value[0] == 'T' || value[0] == 't' || value[0] == 'F' || value[0] == 'f') &&
      value[1] == '\0') {
    return true;
  }
  return strcmp(value, "on") == 0;
}

}  // namespace

void rangeInit() {
  if (!s_prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  const uint8_t saved = s_prefs.getUChar(kPrefsRangeKey, kDefaultRangeIndex);
  s_range_index =
      (saved < kRangePresetCount) ? saved : kDefaultRangeIndex;
  s_use_miles = s_prefs.getBool(kPrefsMilesKey, true);  // default miles
  s_show_runways = s_prefs.getBool(kPrefsRunwaysKey, true);
  s_show_trails = s_prefs.getBool(kPrefsTrailsKey, true);
  s_auto_zoom = s_prefs.getBool(kPrefsAutoZoomKey, false);
  s_prefs.end();
}

void rangeNext() {
  s_range_index = static_cast<uint8_t>((s_range_index + 1) % kRangePresetCount);
  saveRangeIndex();
}

void rangeSetIndex(uint8_t idx) {
  // Transient: used by the auto-zoom tour, which changes the range every few
  // seconds — persisting each step would needlessly wear flash, and the user's
  // last *manual* preset (saved by rangeNext) should be what's restored later.
  if (idx < kRangePresetCount) {
    s_range_index = idx;
  }
}

const RangePreset& rangeCurrent() { return kRangePresets[s_range_index]; }

uint8_t rangeIndex() { return s_range_index; }

float fetchRadiusKm() {
  // Auto-zoom needs to know where traffic is at *every* level to choose a zoom,
  // so always fetch out to the widest preset while it's on (the display still
  // renders only what fits the active range).
  if (s_auto_zoom) {
    return kRangePresets[kRangePresetCount - 1].outer_km;
  }
  // Fetch exactly to the outer ring so the labeled range = the max distance
  // shown: a plane at the rim is ~outer_km away, and nothing farther appears.
  return rangeCurrent().outer_km;
}

bool useMiles() { return s_use_miles; }

bool showRunways() { return s_show_runways; }

bool showTrails() { return s_show_trails; }

bool autoZoom() { return s_auto_zoom; }

void saveMilesFromPortal(const char* checkbox_value) {
  s_use_miles = portalCheckboxChecked(checkbox_value);
  saveUseMiles();
  Serial.printf("Distance units: %s\n", s_use_miles ? "miles" : "km");
}

void saveRunwaysFromPortal(const char* checkbox_value) {
  s_show_runways = portalCheckboxChecked(checkbox_value);
  saveShowRunways();
  Serial.printf("Runway overlay: %s\n", s_show_runways ? "on" : "off");
}

void saveTrailsFromPortal(const char* checkbox_value) {
  s_show_trails = portalCheckboxChecked(checkbox_value);
  saveShowTrails();
  Serial.printf("Flight trails: %s\n", s_show_trails ? "on" : "off");
}

void saveAutoZoomFromPortal(const char* checkbox_value) {
  s_auto_zoom = portalCheckboxChecked(checkbox_value);
  saveAutoZoom();
  Serial.printf("Auto zoom: %s\n", s_auto_zoom ? "on" : "off");
}

void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles) {
  const float value = use_miles ? ring3_km / kKmPerMile : ring3_km;
  const char* unit = use_miles ? "mi" : "km";
  // Round to 0.1; print as a whole number when it is one (e.g. "5mi"), else with
  // a single decimal so fractional presets like 7.5 mi aren't rounded to "8mi".
  const float rounded = roundf(value * 10.0f) / 10.0f;
  if (fabsf(rounded - roundf(rounded)) < 0.05f) {
    snprintf(buf, len, "%d%s", static_cast<int>(lroundf(rounded)), unit);
  } else {
    snprintf(buf, len, "%.1f%s", rounded, unit);
  }
}

void formatCurrentRing3Label(char* buf, size_t len) {
  formatRing3Label(buf, len, rangeCurrent().ring3_km, s_use_miles);
}

void unitsReset() {
  s_use_miles = true;
  s_show_runways = true;
  s_show_trails = true;
  s_auto_zoom = false;
  if (s_prefs.begin(kPrefsNamespace, false)) {
    s_prefs.remove(kPrefsMilesKey);
    s_prefs.remove(kPrefsRunwaysKey);
    s_prefs.remove(kPrefsTrailsKey);
    s_prefs.remove(kPrefsAutoZoomKey);
    s_prefs.end();
  }
}

}  // namespace ui::radar
