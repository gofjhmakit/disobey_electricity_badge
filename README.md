# Nordpool Price Badge

ESP32 Arduino sketch for a wide ST7789 badge that fetches Nordpool electricity prices and displays them with a 24-hour bar chart, color-coded thresholds, and NeoPixel feedback.

## Files

- `NordpoolPriceBadge.ino` — main Arduino sketch
- `LGFX_Config.h` — display configuration for LovyanGFX / ST7789

## Features

- Wi-Fi connection using stored SSID/password
- Nordpool price fetch via `api.spot-hinta.fi`
- Full-day price bar chart and current price display
- Color-coded thresholds and LED ring status
- Settings screen for cheap/moderate thresholds and LED brightness
- Local cache using LittleFS for offline fallback

## Hardware

- Display: ST7789, 320x170
- TFT pins: DC=GPIO15, RST=GPIO7, CS=GPIO6, SCK=GPIO4, MOSI=GPIO5
- NeoPixel data: GPIO18, enable: GPIO17
- Buttons:
  - Up: GPIO11
  - Down: GPIO1
  - Left: GPIO21
  - Right: GPIO2
  - Joystick Press: GPIO14
  - A: GPIO13
  - B: GPIO38
  - Start: GPIO12
  - Select: GPIO45

## Dependencies

- `WiFi` (ESP32 core)
- `HTTPClient`
- `ArduinoJson`
- `LovyanGFX`
- `FastLED`
- `LittleFS`

## Build

1. Open the `NordpoolPriceBadge` sketch in the Arduino IDE or PlatformIO.
2. Select an ESP32 board.
3. Install the listed libraries if needed.
4. Upload and monitor serial output at 115200.

## Notes

- If Wi-Fi credentials are not saved, the device starts in config mode and will use cache only if available.
- Adjust thresholds and LED brightness from the Start menu.
