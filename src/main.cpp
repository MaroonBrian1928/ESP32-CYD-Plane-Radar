/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/timezone.h"
#include "services/wifi_setup.h"
#include "ui/radar_autozoom.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;
unsigned long g_last_anim_ms = 0;
unsigned long g_last_tz_ms = 0;

/** Re-derive the timezone offset periodically (catches DST + location edits). */
constexpr unsigned long kTzRefreshMs = 12UL * 60 * 60 * 1000;  // 12 h

// Set the clock's timezone from the radar coordinates (timeapi.io), falling
// back to the static kTimezone if the lookup fails. NTP keeps the time synced.
void applyTimezone() {
  long offset_sec = 0;
  if (services::timezone::fetchUtcOffsetSeconds(services::location::lat(),
                                                services::location::lon(),
                                                &offset_sec)) {
    configTime(offset_sec, 0, config::kNtpServer1, config::kNtpServer2);
    Serial.printf("Timezone: auto from %.4f,%.4f (UTC%+ld:%02ld)\n",
                  services::location::lat(), services::location::lon(),
                  offset_sec / 3600, (offset_sec < 0 ? -offset_sec : offset_sec) % 3600 / 60);
  } else {
    configTzTime(config::kTimezone, config::kNtpServer1, config::kNtpServer2);
    Serial.println("Timezone: auto lookup failed — using config kTimezone");
  }
  g_last_tz_ms = millis();
}

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  ui::radar::autoZoomReset();  // start the tour from the widest level
  g_radar_visible = true;
}

void onRangeTap() {
  // Auto-zoom owns the range while it's on, so ignore manual range cycling
  // (change it from the setup page). BOOT long-press to reset still works.
  if (ui::radar::autoZoom()) {
    return;
  }
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  // A BOOT tap or a screen tap both cycle the range preset; the BOOT long-press
  // still clears Wi-Fi (handled in bootButtonPollLongPress()).
  if (bootButtonConsumeTap() || displayTouchConsumeTap()) {
    onRangeTap();
  }
}

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    handleBootButton();
    return;
  }
  ui::radarDisplayRefreshAircraft();
  handleBootButton();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");

  bootButtonInit();
  displayInit();
  ui::radarDisplayInit();  // reserve the frame buffer before Wi-Fi fragments the heap
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  ui::radar::rangeInit();
  services::adsb::setPollFn(wifiLoop);

  if (wifiSetupConnect()) {
    showRadarIfConnected();   // draw the radar first
    applyTimezone();          // then derive TZ from coords + start NTP
  }
}

void loop() {
  handleBootButton();
  wifiLoop();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
      g_last_anim_ms = millis();
    } else if (millis() - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
      g_last_adsb_fetch_ms = millis();
      fetchAndDrawAircraft();
      g_last_anim_ms = millis();
    } else if (millis() - g_last_anim_ms >= config::kRadarAnimFrameMs) {
      // In-between frames: redraw with dead-reckoned positions so aircraft glide
      // instead of snapping on each fetch.
      g_last_anim_ms = millis();
      ui::radarDisplayRefreshAircraft();
    }

    // Auto-zoom tour: steps the range toward the closest-detail view with a
    // plane (self-throttled). Redraw right away when it changes the zoom.
    if (ui::radar::autoZoomTick()) {
      ui::radarDisplayDraw();
      g_last_anim_ms = millis();
    }

    if (millis() - g_last_tz_ms >= kTzRefreshMs) {
      applyTimezone();  // refresh offset (DST changeover / edited location)
    }
  }

  delay(10);
}
