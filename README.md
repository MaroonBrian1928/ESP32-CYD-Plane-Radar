# Plane Radar

<img width="800" height="450" alt="plane-radar" src="https://github.com/user-attachments/assets/716d0992-dab8-47ba-8f1a-2aec7f607419" />

**3D printed case (STL + assembly):** [MakerWorld](https://makerworld.com/en/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display#profileId-3207083) · **Firmware:** [Releases](https://github.com/MatixYo/ESP32-Plane-Radar/releases)

Firmware for the **Cheap Yellow Display (ESP32-2432S028R)** — an ESP32-WROOM with a **2.8″ 320×240 ST7789/ILI9341** panel and **XPT2046 resistive touch**. Shows a circular **ADS-B radar** around your configured location, centered on the landscape screen, with **WiFiManager** for first-time setup.

> **Note:** this is a CYD port. The original project targets an **ESP32-C3 Super Mini** with a 1.28″ round GC9A01 (240×240) — see [MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar) for that hardware.

## What it does

1. **Wi‑Fi setup** (if needed) — captive portal on AP **`PlaneRadar-Setup`**
2. **Radar** — live aircraft from [adsb.fi](https://opendata.adsb.fi/) on a sonar-style grid

After Wi‑Fi is saved, the device reconnects automatically; the radar runs in the main loop with periodic ADS-B updates (~5 s).

## Controls

The **BOOT** button (GPIO 0, active LOW) and the **touchscreen** both drive the UI:

| Action | Effect |
|--------|--------|
| **Tap screen** *or* **short tap BOOT** | Cycle range preset (5 → 10 → 15 → 25 km); saved to flash |
| **Hold BOOT 3 s** | Clear Wi‑Fi, location, and units; reboot into setup portal |

During setup you can also hold BOOT at power-on to force a credential reset (same as the long press). Any spot on the screen counts as a tap — no calibration needed for cycling range.

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
| **Display distances in miles** | Ring scale label in **mi** instead of **km** (e.g. `6mi` vs `10km`) |
| **Show airport runways** | Major-airport runway overlay on the radar (off to hide) |

After a reset, the device reboots and shows the setup screen immediately (no “Connecting” loop on stale credentials).

## Radar display

### Grid

- Dark blue background, subdued green rings and crosshairs
- The circular grid is **centered on the 320×240 landscape screen**; the extra width is used for aircraft, tags, and the **E / W** labels in the side margins (**N / S** at top/bottom)
- Range label on the **east** spoke (ring 3 = ¾ of outer radius); white center dot
- Beyond-range direction dots project onto the **rectangular** screen border, so they reach the corners

Layout and colors: `include/ui/radar_theme.h`.

### Range presets

| Ring 3 label | Outer radius (aircraft scale) |
|------------|-------------------------------|
| 5 km / 3 mi | ~6.7 km |
| 10 km / 6 mi | ~13.3 km (default) |
| 15 km / 9 mi | ~20 km |
| 25 km / 16 mi | ~33.3 km |

Preset and miles/km choice persist across reboot (`planeradar` NVS namespace).

### Runways

- Major airports from OurAirports (`large_airport`); all open runway strips in range (helipads excluded)
- Teal runway lines with one ICAO label per airport (e.g. `KJFK`); toggle in the Wi‑Fi setup portal
- Update the embedded list: `python3 scripts/build_large_airports.py`

### Aircraft

- **Inside the outer ring** — red heading triangle, magenta speed vector (clipped at the ring), callsign / type / altitude tags
- **Outside the ring** (still within ADS-B fetch) — small **red dot on the screen rim** at the correct bearing (direction cue; not distance-accurate past the ring)
- **Tags** — placed toward the **center**: west (left) → tag on the **right** of the symbol; east (right) → tag on the **left**

As range decreases (or aircraft approach), targets move inward; beyond-ring dots become full symbols when they cross the outer ring.

### ADS-B

- Source: `https://opendata.adsb.fi/api/v3/`
- Fetch radius: `ui::radar::fetchRadiusKm()` — scales with the active preset to roughly the screen edge (so rim dots have data)
- Poll interval: `kAdsbFetchIntervalMs` (5 s) in `config.h`
- Ground aircraft hidden by default (`kAdsbShowGroundAircraft`)

## Configuration

Edit **`include/config.h`** for hardware and behavior:

| Area | Keys / notes |
|------|----------------|
| Portal | `kPortalApName`, `kPortalIp`, `kPortalHostname` / `kPortalHostUrl` (mDNS; needs `-DWM_MDNS` in `platformio.ini`) |
| Wi‑Fi timing | connect attempts, reconnect grace, portal timeout (`0` = no timeout) |
| BOOT | `kBootPin` (GPIO 0), `kBootResetHoldMs`, `kBootTapMinMs` |
| Display SPI | `kDisplayPin*`, `kDisplayPinBacklight`, `kDisplayInvert`, `kDisplayRgbOrder`, `kDisplaySpiWriteHz` |
| Touch (XPT2046) | `kTouchPinSclk/Mosi/Miso/Cs/Irq` (separate SPI bus) |
| Default location | `kDefaultRadarLat`, `kDefaultRadarLon` (until portal overrides) |
| ADS-B | `kAdsbFetchIntervalMs`, `kAdsbShowGroundAircraft` |

Range presets: `include/ui/radar_range.h` (`kRangePresets`).

**Display tweaks (CYD panel variants).** Defaults match the dual-USB CYD (ST7789, BGR, inversion off). If the first flash looks wrong:

- Image looks like a photo **negative** → flip `kDisplayInvert` in `config.h`
- **Reds/blues swapped** → flip `kDisplayRgbOrder`
- Panel is an **ILI9341** variant, not ST7789 → swap `lgfx::Panel_ST7789` for `lgfx::Panel_ILI9341` in `include/hardware/lgfx_config.hpp`

## Project layout

```
include/
  config.h
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
data/
  ui_font.vlw              — embedded smooth UI font (Noto Sans Bold)
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
- The UI font is embedded in the firmware (`board_build.embed_files`), so there is **no separate SPIFFS/`uploadfs` step** — one firmware upload is all you need

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
