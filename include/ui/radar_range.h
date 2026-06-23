#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::radar {

/**
 * Range presets (label on ring 3 = ¾ of outer radius).
 *
 * Tuned for close-in spotting in miles:
 *   1 mi — pattern / very local (airfield vicinity)
 *   2 mi — default; neighborhood spotting
 *   3 mi — wider local area
 *   5 mi — metro / regional picture
 *  10 mi — wider regional picture
 *
 * The preset value is the OUTER ring (max range) — a plane at the rim is ~that
 * far away. The four rings fall at ¼ / ½ / ¾ / 1 of it. Stored in km; the label
 * shows mi when miles units are on.
 */
struct RangePreset {
  /** Labeled distance = the outer ring (max range), stored in km. */
  float ring3_km;
  /** Outer radius for aircraft math (km). Equal to ring3_km. */
  float outer_km;
};

constexpr float kKmPerMile = 1.609344f;

constexpr RangePreset kRangePresets[] = {
    {1.0f * kKmPerMile, 1.0f * kKmPerMile},
    {2.0f * kKmPerMile, 2.0f * kKmPerMile},
    {3.0f * kKmPerMile, 3.0f * kKmPerMile},
    {5.0f * kKmPerMile, 5.0f * kKmPerMile},
    {10.0f * kKmPerMile, 10.0f * kKmPerMile},
};

constexpr size_t kRangePresetCount =
    sizeof(kRangePresets) / sizeof(kRangePresets[0]);

/** Load saved range and distance units from flash. Call once after boot. */
void rangeInit();
/** Cycle preset and save to flash. */
void rangeNext();
const RangePreset& rangeCurrent();
uint8_t rangeIndex();
/** ADSB fetch radius (km): scaled to screen edge so beyond-ring dots have data. */
float fetchRadiusKm();

bool useMiles();
bool showRunways();
bool showTrails();
/** WiFi portal checkbox: "T" = miles, otherwise km. */
void saveMilesFromPortal(const char* checkbox_value);
void saveRunwaysFromPortal(const char* checkbox_value);
void saveTrailsFromPortal(const char* checkbox_value);
void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles);
void formatCurrentRing3Label(char* buf, size_t len);
/** Reset distance units to km (e.g. with WiFi credential wipe). */
void unitsReset();

}  // namespace ui::radar
