#include "services/radar_location.h"

#include <Preferences.h>
#include <cstdlib>
#include <cstring>

#include "config.h"

// Optional dev-time pre-seed (git-ignored). See include/secrets.example.h.
#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  endif
#endif

#if defined(PLANE_RADAR_LAT) && defined(PLANE_RADAR_LON)
#  define PLANE_RADAR_DEFAULT_LAT (PLANE_RADAR_LAT)
#  define PLANE_RADAR_DEFAULT_LON (PLANE_RADAR_LON)
#else
#  define PLANE_RADAR_DEFAULT_LAT (config::kDefaultRadarLat)
#  define PLANE_RADAR_DEFAULT_LON (config::kDefaultRadarLon)
#endif

namespace services::location {

namespace {

constexpr char kPrefsNamespace[] = "radar";
constexpr char kKeyLat[] = "lat";
constexpr char kKeyLon[] = "lon";

double s_lat = PLANE_RADAR_DEFAULT_LAT;
double s_lon = PLANE_RADAR_DEFAULT_LON;

bool parseCoord(const char* text, double* out) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const double v = strtod(text, &end);
  if (end == text || (end != nullptr && *end != '\0')) {
    return false;
  }
  *out = v;
  return true;
}

bool validLatLon(double lat, double lon) {
  return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

void persist(double lat, double lon) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putDouble(kKeyLat, lat);
  prefs.putDouble(kKeyLon, lon);
  prefs.end();
  s_lat = lat;
  s_lon = lon;
}

}  // namespace

void init() {
#if defined(PLANE_RADAR_FORCE_SECRETS) && defined(PLANE_RADAR_LAT) && \
    defined(PLANE_RADAR_LON)
  // Force-override: stomp whatever is in NVS with the secrets.h coordinates,
  // recovering from corrupted/stale saved values. Persist so it sticks even
  // after the force flag is removed.
  Serial.println("location: forcing coordinates from secrets.h");
  persist(PLANE_RADAR_DEFAULT_LAT, PLANE_RADAR_DEFAULT_LON);
  return;
#endif

  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  if (prefs.isKey(kKeyLat) && prefs.isKey(kKeyLon)) {
    const double lat = prefs.getDouble(kKeyLat, config::kDefaultRadarLat);
    const double lon = prefs.getDouble(kKeyLon, config::kDefaultRadarLon);
    if (validLatLon(lat, lon)) {
      s_lat = lat;
      s_lon = lon;
    }
  }
  prefs.end();
}

double lat() { return s_lat; }

double lon() { return s_lon; }

bool saveFromStrings(const char* lat_str, const char* lon_str) {
  double lat = 0.0;
  double lon = 0.0;
  if (!parseCoord(lat_str, &lat) || !parseCoord(lon_str, &lon)) {
    return false;
  }
  if (!validLatLon(lat, lon)) {
    return false;
  }
  persist(lat, lon);
  Serial.printf("Radar location saved: %.6f, %.6f\n", lat, lon);
  return true;
}

void clear() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.remove(kKeyLat);
  prefs.remove(kKeyLon);
  prefs.end();
  s_lat = PLANE_RADAR_DEFAULT_LAT;
  s_lon = PLANE_RADAR_DEFAULT_LON;
}

}  // namespace services::location
