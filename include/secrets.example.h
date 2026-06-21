// Optional dev-time pre-seed so you can skip the captive-portal setup.
//
//   1. Copy this file to  include/secrets.h   (that name is git-ignored)
//   2. Fill in your Wi-Fi and home coordinates
//   3. Rebuild + flash
//
// On a fresh device (nothing saved in NVS yet) the firmware connects with
// these credentials and centers the radar on these coordinates — no portal.
// Anything you later save via the web portal is stored in NVS and takes
// precedence over these defaults. Delete include/secrets.h to go back to the
// normal first-boot portal flow.
//
// This file is entirely optional: the project builds and runs without a
// secrets.h (it's pulled in via __has_include).

#pragma once

// --- Wi-Fi (required to skip the portal) ---
#define PLANE_RADAR_WIFI_SSID "your_ssid_here"
#define PLANE_RADAR_WIFI_PASS "your_password_here"

// --- Radar center (optional; falls back to config.h defaults if omitted) ---
#define PLANE_RADAR_LAT 52.3676
#define PLANE_RADAR_LON 4.9041

// --- Force override (optional) ---
// Normally values saved on the device (NVS) take precedence over this file, and
// secrets.h only fills in blanks. Uncomment the line below to instead FORCE the
// values above to overwrite whatever is stored on every boot — use this to
// recover from corrupted/stale saved Wi-Fi or coordinates. The forced values
// are persisted, so re-flash with this commented out again for normal use.
// #define PLANE_RADAR_FORCE_SECRETS 1
