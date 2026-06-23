# Plane Radar

<img width="800" height="450" alt="plane-radar" src="https://github.com/user-attachments/assets/716d0992-dab8-47ba-8f1a-2aec7f607419" />

> _Photo and case below are the **original round (GC9A01)** build. This CYD port renders the same radar on a rectangular 320×240 screen._

**3D printed case (STL + assembly):** [MakerWorld](https://makerworld.com/en/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display#profileId-3207083) · **Firmware:** [Releases](https://github.com/MatixYo/ESP32-Plane-Radar/releases)

Firmware for the **Cheap Yellow Display (ESP32-2432S028R)** — an ESP32-WROOM with a **2.8″ 320×240 ST7789/ILI9341** panel and **XPT2046 resistive touch**. Shows a circular **ADS-B radar** around your configured location, centered on the landscape screen, with **WiFiManager** for first-time setup. Aircraft are drawn at their **real positions** and glide smoothly between updates.

> **Note:** this is a CYD port. The original project targets an **ESP32-C3 Super Mini** with a 1.28″ round GC9A01 (240×240) — see [MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar) for that hardware.

## What it does

1. **Wi‑Fi setup** (if needed) — captive portal on AP **`PlaneRadar-Setup`**
2. **Radar** — live aircraft from [adsb.fi](https://opendata.adsb.fi/) on a sonar-style grid

After Wi‑Fi is saved, the device reconnects automatically; the radar runs in the main loop with periodic ADS-B updates (~1.5 s), and **dead-reckons** each aircraft along its track between updates so motion is smooth, not snappy.

## Controls

The **BOOT** button (GPIO 0, active LOW) and the **touchscreen** both drive the UI:

| Action | Effect |
|--------|--------|
| **Tap screen** *or* **short tap BOOT** | Cycle range preset (1 → 2 → 3 → 5 → 10 mi); saved to flash |
| **Hold BOOT 3 s** | Clear Wi‑Fi, location, and units; reboot into setup portal |

Any spot on the screen counts as a tap (latched on the touch IRQ, so it never misses a tap even mid-render — no calibration needed). The hold-to-reset only arms **after** BOOT has been released once, so the USB programming circuit (which can pull GPIO 0 low on a tethered board) can't trigger a spurious Wi-Fi wipe.

## Wi‑Fi setup portal

**First-time setup** (no saved Wi‑Fi):

1. Connect to **`PlaneRadar-Setup`**
2. Open **`http://plane-radar.local`** (preferred) or **`http://192.168.4.1`** — both are shown on the yellow setup screen; captive portal may open automatically
3. Set home Wi‑Fi, then save

**Reconfigure anytime** (after the device is on your network):

1. Open **`http://plane-radar.local`** or **`http://<device-ip>`** (e.g. from your router or serial log at boot)
2. Change Wi‑Fi, location, units, or runway overlay; save

The same portal runs on the setup AP and on the device’s LAN IP while connected to Wi‑Fi. mDNS hostname is `plane-radar` → **plane-radar.local** (`kPortalHostname` in `config.h`). Some clients resolve `.local` slowly; use the IP if needed.

**Custom fields** (stored in NVS):

| Field | Purpose |
|-------|---------|
| **Latitude / Longitude** | Radar center and ADS-B query position (defaults in `config.h` until set) |
| **Display distances in miles** | Range label + aircraft distances in **mi** (default) instead of **km** (e.g. `2mi` vs `3km`) |
| **Show airport runways** | Major-airport runway overlay on the radar (off to hide) |

After a reset, the device reboots and shows the setup screen immediately (no “Connecting” loop on stale credentials).

## Radar display

### Grid

- True-black background, subdued green rings and crosshairs
- The circular grid is **centered on the 320×240 landscape screen**; the extra width is used for aircraft, tags, the **E / W** labels in the side margins (**N / S** at top/bottom), and a corner legend
- Range label (the **outer ring / max range**) in the **top-right** corner; white center dot
- A **plane ▲ / heli ●** legend sits in the bottom-left
- **Top-left:** aircraft count + a feed-freshness dot (green, pulses on each fetch; red if updates stall > 6 s)
- **Bottom-right:** NTP clock (`HH:MM`), once time is synced — **timezone is auto-derived from your coordinates** (via timeapi.io, DST-aware), so no manual setup dead space

Layout and colors: `include/ui/radar_theme.h`.

### Range presets

Mile-based, tuned for close-in spotting. The **labeled value is the outer ring (max range)** — a plane at the rim is ~that far away, and nothing farther is shown. Rings fall at ¼ / ½ / ¾ / 1 of it.

| Preset (outer ring) | Notes |
|---------------------|-------|
| 1 mi | pattern / very local |
| 2 mi | default; neighborhood |
| 3 mi | wider local area |
| 5 mi | metro / regional |
| 10 mi | wider regional |

Stored in km internally; the label shows **mi** when miles units are on (default) — see `kRangePresets` in `include/ui/radar_range.h`. Preset and units persist across reboot (`planeradar` NVS namespace).

### Runways

- Major airports from OurAirports (`large_airport`); all open runway strips in range (helipads excluded)
- Teal runway lines with one ICAO label per airport (e.g. `KJFK`); toggle in the Wi‑Fi setup portal
- Update the embedded list: `python3 scripts/build_large_airports.py`

### Aircraft

- Every aircraft is drawn at its **real position** (equirectangular projection, so distances are accurate at any latitude). There are **no fake rim dots** — only real positions.
- **Planes** — red heading **triangle** with a magenta **track vector** (length ∝ ground speed, pointing along the ground track)
- **Helicopters** (ADS-B category `A7`) — red **circle**; a **▲ Plane / ● Heli** legend sits in the bottom-left
- **Tags** (compact font) — **callsign**, **type**, and **distance** (e.g. `2.3mi`), placed toward the center: west (left) → tag on the **right** of the symbol; east (right) → tag on the **left**
- **Flight trails** — a dim line of each aircraft's recent path (up to ~8 min, `kTrailLen`), shown only when fewer than `kTrailMaxAircraft` (10) are in range so busy views stay clean; history is recorded in a small dedicated pool (16 slots, int16 center-offsets) so trails can be long without starving the heap

Between the ~1.5 s updates, each aircraft is **dead-reckoned** along its track at its ground speed, and the displayed position is eased toward each new fix, so motion is smooth rather than snapping.

### ADS-B

- Source: `https://opendata.adsb.fi/api/v3/` (HTTPS, keep-alive connection reused across fetches)
- Fields used: `lat` `lon` `track` `true_heading`/`mag_heading` `gs` `flight` `hex` `t` `alt_baro` `category`; the JSON is **filtered** to just these so big responses don't exhaust the heap
- Fetch radius: `ui::radar::fetchRadiusKm()` = the active preset's outer ring
- Poll interval: `kAdsbFetchIntervalMs` (1.5 s) in `config.h` — adsb.fi's public limit is 1 req/s
- Ground aircraft hidden by default (`kAdsbShowGroundAircraft`)

### Rendering

- The frame is composited off-screen into a **4-bit paletted sprite** (~38 KB) and blitted in one `pushSprite` — flicker-free, exact palette colors, and half the RAM of an 8-bit buffer (which keeps the heap free for Wi-Fi/TLS).
- A second **cached static layer** (rings/labels/runways/legend) is rendered once per range change and `memcpy`'d each frame, so only the aircraft are redrawn — high frame rate.
- All drawing is **solid (non-anti-aliased)**: paletted sprites can't alpha-blend, so lines/circles/text are crisp. Frame cadence: `kRadarAnimFrameMs`; panel SPI: `kDisplaySpiWriteHz`.

## Configuration

Edit **`include/config.h`** for hardware and behavior:

| Area | Keys / notes |
|------|----------------|
| Portal | `kPortalApName`, `kPortalIp`, `kPortalHostname` / `kPortalHostUrl` (mDNS; needs `-DWM_MDNS` in `platformio.ini`); `kCaptivePortalEnabled` = auto-open setup AP on connect failure (set false for headless / secrets.h use — BOOT-hold reset still opens it) |
| Wi‑Fi timing | connect attempts, reconnect grace, portal timeout (`0` = no timeout) |
| BOOT | `kBootPin` (GPIO 0), `kBootResetHoldMs`, `kBootTapMinMs` |
| Display SPI | `kDisplayPin*`, `kDisplayPinBacklight`, `kDisplayInvert`, `kDisplayRgbOrder`, `kDisplaySpiWriteHz` |
| Touch (XPT2046) | `kTouchPinSclk/Mosi/Miso/Cs/Irq` (separate SPI bus) |
| Default location | `kDefaultRadarLat`, `kDefaultRadarLon` (until portal overrides) |
| ADS-B | `kAdsbFetchIntervalMs` (≥1000), `kAdsbShowGroundAircraft` |
| Trails | `kTrailMaxAircraft` (draw threshold); `kTrailLen` in `radar_display.cpp` (history length) |
| Motion / fps | `kRadarAnimFrameMs` (frame cadence), `kRadarExtrapMaxSec` (dead-reckon cap) |
| Clock | timezone auto-derived from coordinates (timeapi.io); `kTimezone` (POSIX TZ) is the offline fallback; `kNtpServer1/2` |

Range presets: `include/ui/radar_range.h` (`kRangePresets`).

### Skip the setup portal during development

Copy `include/secrets.example.h` to **`include/secrets.h`** (git-ignored) and fill in your Wi-Fi + home coordinates. On a fresh device the firmware connects with those and centers the radar there — no captive portal. Anything saved later via the web portal (NVS) takes precedence. Optional `#define PLANE_RADAR_FORCE_SECRETS 1` forces these to overwrite saved values on the next boot (one-time recovery); remove it again for normal operation. The file is pulled in via `__has_include`, so the project still builds without it.

**Display tweaks (CYD panel variants).** Defaults match the dual-USB CYD (ST7789, BGR, inversion off). If the first flash looks wrong:

- Image looks like a photo **negative** → flip `kDisplayInvert` in `config.h`
- **Reds/blues swapped** → flip `kDisplayRgbOrder` (also swaps the aircraft red in `applyFramePalette`)
- Panel is an **ILI9341** variant, not ST7789 → swap `lgfx::Panel_ST7789` for `lgfx::Panel_ILI9341` in `include/hardware/lgfx_config.hpp`
- Speckle/noise at high SPI clock → lower `kDisplaySpiWriteHz` (e.g. 55 MHz → 40 MHz)

## Project layout

```
include/
  config.h
  secrets.example.h        — copy to secrets.h (git-ignored) to skip the portal
  hardware/
    lgfx_config.hpp
    display.h
    display_font.h
  data/
    large_airports.h
  ui/
    radar_theme.h
    radar_range.h
    radar_display.h
    runway_overlay.h
    status_screens.h
  services/
    wifi_setup.h
    radar_location.h
    adsb_client.h
scripts/
  build_large_airports.py
  flash.sh                 — build + upload over USB serial (no sudo)
  merge-firmware.sh        — single web-flashable .bin
src/
  main.cpp
  data/
    large_airports_data.cpp
  hardware/
  ui/
  services/
```

## Wiring (CYD / ESP32-2432S028R)

The CYD is a self-contained board — the display, touch panel, backlight, and BOOT button are already wired. These are the pins the firmware uses (in `config.h`); no external wiring is needed:

| Function | ESP32 GPIO |
|----------|------------|
| Display SCLK | **14** |
| Display MOSI | **13** |
| Display MISO | **12** |
| Display CS | **15** |
| Display DC | **2** |
| Display RST | tied to EN (−1) |
| Backlight | **21** |
| Touch SCLK | **25** |
| Touch MOSI | **32** |
| Touch MISO | **39** |
| Touch CS | **33** |
| Touch IRQ | **36** |
| BOOT (user button) | **0** |

## Build

```bash
pio run -e cyd -t upload
pio device monitor
```

Or use the helper (auto-detects `pio`, checks the serial port, no sudo):

```bash
chmod +x scripts/flash.sh            # once
PORT=/dev/ttyUSB0 scripts/flash.sh   # build + upload firmware
scripts/flash.sh monitor             # open the serial monitor
```

- PlatformIO env: **`cyd`** (board `esp32dev`)
- Serial: **115200** baud
- Fonts are compiled into the firmware (built-in bitmap GFX fonts), so there is **no SPIFFS/`uploadfs` step** — one firmware upload is all you need

### Web-flashable release image

Single `.bin` for [esptool-js](https://espressif.github.io/esptool-js/) and similar tools (ESP32, 4 MB, flash at **0x0**):

```bash
chmod +x scripts/merge-firmware.sh   # once
./scripts/merge-firmware.sh
```

Writes `release/plane-radar-merged.bin`. Skip rebuild if firmware is already built:

```bash
./scripts/merge-firmware.sh --no-build
```

Or via PlatformIO only (output: `.pio/build/cyd/firmware-merged.bin`):

```bash
pio run -e cyd
pio run -t merge -e cyd
```

Put the board in download mode (hold **BOOT**, tap **RESET**), then flash with Chrome/Edge over USB.

### CI and releases (GitHub Actions)

| Workflow | When | Output |
|----------|------|--------|
| [Build](.github/workflows/build.yml) | Push / PR to `main` | Artifact `plane-radar-cyd` (merged + split `.bin` files, ~90 days) |
| [Release](.github/workflows/release.yml) | Git tag `v*` (e.g. `v1.0.0`) | GitHub Release asset `plane-radar-v1.0.0.bin` + `.sha256` |

To ship a version users can download:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The release workflow builds firmware in CI and attaches the merged image to the release. Download from **Releases** on GitHub, then flash at **0x0** (ESP32, 4 MB).

## Dependencies

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
