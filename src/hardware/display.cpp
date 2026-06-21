#include "hardware/display.h"

#include <Arduino.h>

#include "config.h"
#include "hardware/display_font.h"

LGFX tft;

namespace {

// Touch is detected purely from the XPT2046 PENIRQ line (GPIO36, active LOW) via
// a hardware interrupt — we never call getTouch(). Polling getTouch over SPI
// both misses taps when a heavy render starves the loop AND pokes PENIRQ, which
// causes spurious edges. With interrupt-only, PENIRQ fires exactly once per
// finger-down regardless of how busy the loop is.
volatile bool s_touch_pending = false;
volatile unsigned long s_touch_last_ms = 0;

/** Debounce: collapse contact bounce / a held press into a single tap. */
constexpr unsigned long kTouchDebounceMs = 300;

void IRAM_ATTR onTouchIsr() {
  const unsigned long now = millis();
  if (now - s_touch_last_ms >= kTouchDebounceMs) {
    s_touch_last_ms = now;
    s_touch_pending = true;
  }
}

}  // namespace

void displayInit() {
  tft.init();
  tft.setRotation(1);  // landscape 320×240 (USB on the right)
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();

  // PENIRQ idles HIGH (external pull-up on the CYD) and pulses LOW on touch.
  pinMode(static_cast<uint8_t>(config::kTouchPinIrq), INPUT);
  attachInterrupt(digitalPinToInterrupt(static_cast<uint8_t>(config::kTouchPinIrq)),
                  onTouchIsr, FALLING);
}

bool displayTouchConsumeTap() {
  if (!s_touch_pending) {
    return false;
  }
  s_touch_pending = false;
  return true;
}
