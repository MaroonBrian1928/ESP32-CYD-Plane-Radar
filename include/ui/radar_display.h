#pragma once

namespace ui {

/**
 * Reserve the off-screen frame buffer early (before Wi-Fi/TLS fragments the
 * heap). Call once at boot, right after the display is initialized. Without
 * this the full-screen double buffer fails to allocate on the no-PSRAM CYD and
 * the radar falls back to flickery direct drawing.
 */
void radarDisplayInit();

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

}  // namespace ui
