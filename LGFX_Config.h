#pragma once
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI _bus;

public:
  LGFX(void)
  {
    {
      auto cfg = _bus.config();

      cfg.spi_host = SPI2_HOST;   // ESP32-S3: usually works
      cfg.spi_mode = 0;

      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;

      cfg.spi_3wire  = true;
      cfg.use_lock   = true;

      cfg.dma_channel = SPI_DMA_CH_AUTO;

      cfg.pin_sclk = 4;
      cfg.pin_mosi = 5;
      cfg.pin_miso = -1;
      cfg.pin_dc   = 15;

      _bus.config(cfg);
      _panel.setBus(&_bus);
    }

    {
      auto cfg = _panel.config();

      cfg.pin_cs   = 6;
      cfg.pin_rst  = 7;
      cfg.pin_busy = -1;

      cfg.memory_width  = 170;
      cfg.memory_height = 320;

      cfg.panel_width   = 170;
      cfg.panel_height  = 320;

      cfg.offset_x = -30;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false; // No MISO connection for this display setup

      cfg.invert    = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;

      _panel.config(cfg);
    }

    setPanel(&_panel);
  }
};
