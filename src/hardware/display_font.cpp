#include "hardware/display_font.h"

#include "hardware/display.h"

// The UI uses only bitmap (non-anti-aliased) fonts. The radar composites into a
// 4-bit paletted sprite, and anti-aliased VLW glyphs blend colors that have no
// exact palette entry — which faults the paletted read path. Bitmap glyphs are
// solid fg/bg, so they're palette-safe. The smooth-font (VLW) path is therefore
// stubbed out: displayFontIsSmooth() is always false, so callers take their
// bitmap branch.

bool displayFontInit() { return false; }

bool displayFontIsSmooth() { return false; }

bool displayFontEnsureLoaded(lgfx::LGFXBase&) { return false; }

void displayFontSetSmoothSize(lgfx::LGFXBase& gfx, float size) {
  gfx.setTextSize(size);  // only reached on the (dead) smooth path
}

void displayFontSetBitmap(lgfx::LGFXBase& gfx, const lgfx::GFXfont* font) {
  gfx.setFont(font);
  gfx.setTextSize(1);
}
