#pragma once

#include "hardware/lgfx_config.hpp"

extern LGFX tft;

void displayInit();

/**
 * Poll the XPT2046 touch panel. Returns true exactly once per completed tap
 * (a brief press-and-release), so the caller can treat a screen tap like a
 * BOOT-button tap (cycle range preset). Anywhere on the screen counts.
 */
bool displayTouchConsumeTap();
