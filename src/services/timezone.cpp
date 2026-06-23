#include "services/timezone.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

namespace services::timezone {

bool fetchUtcOffsetSeconds(double lat, double lon, long* out_offset_sec) {
  String url = "https://timeapi.io/api/timezone/coordinate?latitude=";
  url += String(lat, 5);
  url += "&longitude=";
  url += String(lon, 5);

  WiFiClientSecure client;
  client.setInsecure();  // public API; skip cert validation

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("tz: begin failed");
    return false;
  }
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  http.setUserAgent("PlaneRadar/1.0");

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("tz: HTTP %d\n", code);
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("tz: JSON %s (len %u)\n", err.c_str(), payload.length());
    return false;
  }

  JsonVariant secs = doc["currentUtcOffset"]["seconds"];
  if (!secs.is<long>() && !secs.is<int>()) {
    Serial.println("tz: no currentUtcOffset.seconds");
    return false;
  }
  *out_offset_sec = secs.as<long>();
  Serial.printf("tz: UTC offset %ld s\n", *out_offset_sec);
  return true;
}

}  // namespace services::timezone
