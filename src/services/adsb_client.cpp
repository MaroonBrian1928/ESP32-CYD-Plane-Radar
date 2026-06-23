#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cstring>

#include "config.h"

namespace services::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
constexpr int kConnectTimeoutMs = 8000;  // allow time for the TLS handshake
constexpr unsigned long kRequestTimeoutMs = 10000;

Aircraft s_aircraft[kMaxAircraft];
size_t s_aircraft_count = 0;
unsigned long s_last_update_ms = 0;
PollFn s_poll_fn = nullptr;

// Persistent TLS client + HTTPClient so the (slow, ~1-2 s) TLS handshake happens
// once and subsequent fetches reuse the same keep-alive connection. Recreating
// these per fetch caused repeated handshakes that often failed (start_ssl_client
// -1) and stretched the effective update interval to ~10 s.
WiFiClientSecure s_client;
HTTPClient s_http;
bool s_https_init = false;

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

void ensureHttpsInit() {
  if (s_https_init) {
    return;
  }
  s_client.setInsecure();   // adsb.fi is public; skip cert validation
  s_http.setReuse(true);    // HTTP keep-alive: hold the TLS connection open
  s_https_init = true;
}

int httpGet(const String& url) {
  if (!s_http.begin(s_client, url)) {
    return HTTPC_ERROR_CONNECTION_REFUSED;
  }
  s_http.setConnectTimeout(kConnectTimeoutMs);
  s_http.setTimeout(kRequestTimeoutMs);
  pollNetwork();
  return s_http.GET();
}

bool readResponseBodyWithPoll(HTTPClient& http, String& payload) {
  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    return false;
  }

  const int content_length = http.getSize();
  if (content_length > 0) {
    payload.reserve(static_cast<unsigned>(content_length + 1));
  }

  uint8_t buffer[512];
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  while (millis() < deadline) {
    pollNetwork();
    const int available = stream->available();
    if (available > 0) {
      const int to_read =
          available > static_cast<int>(sizeof(buffer)) ? static_cast<int>(sizeof(buffer))
                                                       : available;
      const int read_bytes = stream->readBytes(buffer, to_read);
      if (read_bytes > 0) {
        payload.concat(reinterpret_cast<const char*>(buffer),
                       static_cast<unsigned>(read_bytes));
      }
    }
    if (content_length > 0 &&
        static_cast<int>(payload.length()) >= content_length) {
      break;
    }
    if (!http.connected() && stream->available() <= 0) {
      break;
    }
    delay(1);
  }

  return payload.length() > 0;
}

float kmToNauticalMiles(float km) { return km / kKmPerNm; }

bool readJsonFloat(const JsonObject& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

float pickNoseHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickTrackHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickGroundSpeed(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "gs", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "tas", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "ias", &v)) {
    return v;
  }
  return 0.0f;
}

bool isOnGround(const JsonObject& plane) {
  if (!plane["alt_baro"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

void copyJsonStringTrimmed(const JsonObject& obj, const char* key, char* out,
                           size_t out_len) {
  out[0] = '\0';
  if (out_len == 0 || !obj[key].is<const char*>()) {
    return;
  }
  const char* s = obj[key].as<const char*>();
  size_t n = strnlen(s, out_len - 1);
  while (n > 0 && s[n - 1] == ' ') {
    --n;
  }
  memcpy(out, s, n);
  out[n] = '\0';
}

void formatAltitudeTag(const JsonObject& plane, char* out, size_t out_len) {
  out[0] = '\0';
  if (out_len == 0) {
    return;
  }

  if (plane["alt_baro"].is<const char*>()) {
    const char* s = plane["alt_baro"].as<const char*>();
    if (strcmp(s, "ground") == 0) {
      strncpy(out, "GND", out_len - 1);
      out[out_len - 1] = '\0';
      return;
    }
  }

  float alt = 0.0f;
  if (readJsonFloat(plane, "alt_baro", &alt) ||
      readJsonFloat(plane, "alt_geom", &alt)) {
    snprintf(out, out_len, "%d ft", static_cast<int>(lroundf(alt)));
  }
}

bool isRotorcraft(const JsonObject& plane) {
  // ADS-B emitter category "A7" = rotorcraft (helicopter).
  if (!plane["category"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["category"].as<const char*>(), "A7") == 0;
}

void fillTagFields(Aircraft* ac, const JsonObject& plane) {
  copyJsonStringTrimmed(plane, "hex", ac->hex, sizeof(ac->hex));
  copyJsonStringTrimmed(plane, "flight", ac->callsign, sizeof(ac->callsign));
  if (ac->callsign[0] == '\0') {
    copyJsonStringTrimmed(plane, "hex", ac->callsign, sizeof(ac->callsign));
  }

  copyJsonStringTrimmed(plane, "t", ac->type, sizeof(ac->type));
  formatAltitudeTag(plane, ac->alt, sizeof(ac->alt));
}

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

size_t aircraftCount() { return s_aircraft_count; }

const Aircraft* aircraftList() { return s_aircraft; }

unsigned long lastUpdateMillis() { return s_last_update_ms; }

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km) {
  const float dist_nm = kmToNauticalMiles(fetch_radius_km);

  String url = kApiBase;
  url += String(center_lat, 6);
  url += "/lon/";
  url += String(center_lon, 6);
  url += "/dist/";
  url += String(dist_nm, 1);

  ensureHttpsInit();

  int code = httpGet(url);
  if (code <= 0) {
    // A reused keep-alive socket the server already closed (or a failed
    // handshake) shows up as a negative code — drop it and retry once fresh.
    s_http.end();
    s_client.stop();
    code = httpGet(url);
  }
  if (code != HTTP_CODE_OK) {
    Serial.printf("adsb: HTTP %d\n", code);
    s_http.end();
    return false;
  }

  String payload;
  if (!readResponseBodyWithPoll(s_http, payload)) {
    Serial.println("adsb: empty response");
    s_http.end();
    return false;
  }
  s_http.end();  // setReuse(true) keeps the underlying TLS socket alive

  // Only deserialize the handful of fields the radar uses. adsb.fi returns ~40
  // fields per aircraft; parsing all of them blows the heap (NoMemory) on big
  // responses. A filter keeps the document tiny regardless of aircraft count.
  JsonDocument filter;
  JsonObject f = filter["ac"].add<JsonObject>();
  for (const char* key :
       {"lat", "lon", "true_heading", "mag_heading", "track", "dir", "gs",
        "tas", "ias", "flight", "hex", "t", "alt_baro", "alt_geom",
        "category", "seen_pos"}) {
    f[key] = true;
  }

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("adsb: JSON parse error: %s\n", err.c_str());
    return false;
  }

  JsonArray ac = doc["ac"].as<JsonArray>();
  if (ac.isNull()) {
    s_aircraft_count = 0;
    s_last_update_ms = millis();
    return true;
  }

  size_t n = 0;
  for (JsonObject plane : ac) {
    if (n >= kMaxAircraft) {
      break;
    }
    if (!plane["lat"].is<float>() || !plane["lon"].is<float>()) {
      continue;
    }
    if (isOnGround(plane) && !config::kAdsbShowGroundAircraft) {
      continue;
    }

    s_aircraft[n].lat = plane["lat"].as<float>();
    s_aircraft[n].lon = plane["lon"].as<float>();
    s_aircraft[n].nose_deg = pickNoseHeading(plane);
    s_aircraft[n].track_deg = pickTrackHeading(plane);
    s_aircraft[n].gs_knots = pickGroundSpeed(plane);
    // Age of this position fix (s). The feed reports positions already a few
    // seconds stale; carrying this lets the display dead-reckon from the fix's
    // real measurement time so successive predictions line up (no snap-back).
    s_aircraft[n].seen_pos_s =
        plane["seen_pos"].is<float>() ? plane["seen_pos"].as<float>() : 0.0f;
    s_aircraft[n].is_helicopter = isRotorcraft(plane);
    fillTagFields(&s_aircraft[n], plane);
    ++n;
  }

  s_aircraft_count = n;
  s_last_update_ms = millis();
  Serial.printf("adsb: %u aircraft\n", static_cast<unsigned>(n));
  return true;
}

}  // namespace services::adsb
