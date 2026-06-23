#pragma once

namespace ui::radar {

/**
 * Auto-zoom "tour": when the auto-zoom setting is on, the active range is driven
 * automatically — it starts at the widest preset (so any nearby traffic is in
 * view), then steps inward one level at a time, dwelling at each, until it
 * reaches the most-detailed level that still contains at least one aircraft. It
 * lingers there, then jumps back out and walks in again. If nothing is in range
 * at any level it parks at the widest preset, scanning for traffic.
 */

/** Restart the tour from the widest level (call on connect / when re-enabled). */
void autoZoomReset();

/**
 * Advance the tour state machine. Self-throttled (only acts every few seconds),
 * so it's safe to call every loop iteration. Returns true if it changed the
 * active range this call, so the caller can redraw immediately. No-op when the
 * auto-zoom setting is off.
 */
bool autoZoomTick();

}  // namespace ui::radar
