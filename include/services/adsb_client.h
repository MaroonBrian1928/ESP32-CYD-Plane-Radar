#pragma once

#include <cstddef>

namespace services::adsb {

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  char hex[7];       // ICAO 24-bit address (stable id across updates)
  char callsign[9];
  char type[5];
  char alt[12];
  bool is_helicopter;  // ADS-B emitter category A7 (rotorcraft)
};

constexpr size_t kMaxAircraft = 64;

size_t aircraftCount();
const Aircraft* aircraftList();

/** millis() at the last successful fetch (for dead-reckoning between updates). */
unsigned long lastUpdateMillis();

/** Hook invoked during long HTTP I/O (e.g. wifiLoop). Optional. */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/** Fetch aircraft within fetch_radius_km of center_lat/lon from adsb.fi. */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km);

}  // namespace services::adsb
