#pragma once

#include <cstdint>

#include <driver/gpio.h>

namespace config {

// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "PlaneRadar-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
/** mDNS host (no ".local" suffix); browser: http://plane-radar.local */
constexpr char kPortalHostname[] = "plane-radar";
constexpr char kPortalHostUrl[] = "plane-radar.local";

/** Per-attempt STA connect wait (ms); retried kWifiConnectAttempts times. */
constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;  // 0 = no timeout while configuring
constexpr unsigned long kWifiConnectingFrameMs = 50;
/** Wait after disconnect before reconnecting (avoids portal on brief drops). */
constexpr unsigned long kWifiDownGraceMs = 4000;
/** Minimum interval between background reconnect tries. */
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

// --- BOOT button (CYD / ESP32-WROOM, active LOW, strapping pin GPIO0) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_0;
constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Display: ST7789/ILI9341 2.8" 320×240 landscape (SPI), CYD ESP32-2432S028R ---
// CYD wiring (these match the System-Monitor build). The panel is wired to the
// ESP32 VSPI/HSPI bus; DC/CS/SCLK/MOSI/MISO are fixed by the board.
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_NC;  // -1: tied to EN on the CYD
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_15;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_2;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_13;
constexpr gpio_num_t kDisplayPinMiso = GPIO_NUM_12;
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_14;
/** Backlight (active HIGH on the CYD). */
constexpr gpio_num_t kDisplayPinBacklight = GPIO_NUM_21;

// --- Resistive touch: XPT2046 on a separate SPI bus (CYD) ---
constexpr gpio_num_t kTouchPinSclk = GPIO_NUM_25;
constexpr gpio_num_t kTouchPinMosi = GPIO_NUM_32;
constexpr gpio_num_t kTouchPinMiso = GPIO_NUM_39;
constexpr gpio_num_t kTouchPinCs = GPIO_NUM_33;
constexpr gpio_num_t kTouchPinIrq = GPIO_NUM_36;

// Logical landscape resolution (after rotation). Native panel is 240×320.
constexpr int kDisplayWidth = 320;
constexpr int kDisplayHeight = 240;

constexpr uint32_t kDisplaySpiWriteHz = 55000000;  // CYD ST7789 handles 55 MHz
// DISPLAY TWEAKS (match these against your panel; values below mirror the
// known-good System-Monitor settings for the dual-USB CYD/ST7789):
//   * Colors look like a photo negative  -> flip kDisplayInvert.
//   * Reds and blues are swapped         -> flip kDisplayRgbOrder.
constexpr bool kDisplayInvert = false;
constexpr bool kDisplayRgbOrder = false;  // false = BGR (CYD default)

// --- Radar center defaults (overridden via WiFi setup portal) ---
constexpr double kDefaultRadarLat = 52.3676;
constexpr double kDefaultRadarLon = 4.9041;

/** Poll adsb.fi (API public limit: 1 req/s — don't go below 1000). */
constexpr unsigned long kAdsbFetchIntervalMs = 1500;
/** Legacy scale unused — fetch uses radar::fetchRadiusKm() to screen edge. */
constexpr float kAdsbFetchRadiusScale = 1.0f;
/** false = hide aircraft with alt_baro "ground"; true = show them too. */
constexpr bool kAdsbShowGroundAircraft = false;

// --- Motion smoothing (dead-reckoning between ADS-B updates) ---
/** Redraw cadence for the in-between animation frames (~50 fps target; actual
 *  rate is capped by render time, which the cached grid layer keeps low). */
constexpr unsigned long kRadarAnimFrameMs = 20;
/** Stop extrapolating if updates stall, so stale targets don't fly away (s). */
constexpr float kRadarExtrapMaxSec = 8.0f;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config
