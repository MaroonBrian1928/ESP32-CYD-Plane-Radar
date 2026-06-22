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
 *  50 mi — wide-area (catch distant traffic)
 *
 * Stored in km internally (the label formatter converts to mi when miles units
 * are on). Outer radius (for aircraft math) is ring-3 distance ÷ 0.75.
 */
struct RangePreset {
  /** Distance shown on ring 3 (¾ of outer radius), always stored in km. */
  float ring3_km;
  float outer_km;
};

constexpr float kRing3ToOuterKm = 4.0f / 3.0f;
constexpr float kKmPerMile = 1.609344f;

constexpr RangePreset kRangePresets[] = {
    {1.0f * kKmPerMile, 1.0f * kKmPerMile * kRing3ToOuterKm},
    {2.0f * kKmPerMile, 2.0f * kKmPerMile * kRing3ToOuterKm},
    {3.0f * kKmPerMile, 3.0f * kKmPerMile * kRing3ToOuterKm},
    {5.0f * kKmPerMile, 5.0f * kKmPerMile * kRing3ToOuterKm},
    {50.0f * kKmPerMile, 50.0f * kKmPerMile * kRing3ToOuterKm},
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
/** WiFi portal checkbox: "T" = miles, otherwise km. */
void saveMilesFromPortal(const char* checkbox_value);
void saveRunwaysFromPortal(const char* checkbox_value);
void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles);
void formatCurrentRing3Label(char* buf, size_t len);
/** Reset distance units to km (e.g. with WiFi credential wipe). */
void unitsReset();

}  // namespace ui::radar
