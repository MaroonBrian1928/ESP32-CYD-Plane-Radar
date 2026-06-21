#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"

/**
 * LovyanGFX device for the Cheap Yellow Display (ESP32-2432S028R):
 * ST7789/ILI9341 320×240 panel on SPI2, PWM backlight, and an XPT2046
 * resistive touch controller on a separate SPI bus. Pins come from config.h.
 */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
  lgfx::Panel_ST7789 _panel;  // ILI9341 variant? swap to lgfx::Panel_ILI9341
  lgfx::Light_PWM _light;
  lgfx::Touch_XPT2046 _touch;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = config::kDisplaySpiWriteHz;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = static_cast<int>(config::kDisplayPinSclk);
      cfg.pin_mosi = static_cast<int>(config::kDisplayPinMosi);
      cfg.pin_miso = static_cast<int>(config::kDisplayPinMiso);
      cfg.pin_dc = static_cast<int>(config::kDisplayPinDc);
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = static_cast<int>(config::kDisplayPinCs);
      cfg.pin_rst = static_cast<int>(config::kDisplayPinRst);
      cfg.pin_busy = -1;
      // Native panel memory is 240 (W) × 320 (H); rotation in displayInit()
      // turns it into the 320×240 landscape the UI draws in.
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.readable = false;
      cfg.invert = config::kDisplayInvert;
      cfg.rgb_order = config::kDisplayRgbOrder;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = static_cast<int>(config::kDisplayPinBacklight);
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 0;
      cfg.x_max = 4095;
      cfg.y_min = 0;
      cfg.y_max = 4095;
      // Poll the XPT2046 over SPI every read instead of gating on the IRQ
      // (PENIRQ) line. On many CYD boards the IRQ pin is flaky/uninterrupting,
      // and LovyanGFX skips the SPI read entirely when it reads "not pressed",
      // so touches are silently missed. -1 = always read.
      cfg.pin_int = -1;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.spi_host = SPI3_HOST;  // separate bus from the display
      cfg.freq = 1000000;
      cfg.pin_sclk = static_cast<int>(config::kTouchPinSclk);
      cfg.pin_mosi = static_cast<int>(config::kTouchPinMosi);
      cfg.pin_miso = static_cast<int>(config::kTouchPinMiso);
      cfg.pin_cs = static_cast<int>(config::kTouchPinCs);
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};
