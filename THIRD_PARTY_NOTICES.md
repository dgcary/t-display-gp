# Third-Party Notices

This project is implemented as a modular T-Display-S3 application. It does not
copy an upstream monolithic sketch wholesale. The following open-source projects
are used as libraries, authoritative hardware references, or design/code
references.

## Design and hardware references

- **Zaitronics/esp32-cyd-stock-ticker** — MIT License. Reference for ESP32 stock
  ticker configuration flow, WiFiManager usage, and local web configuration.
  https://github.com/Zaitronics/esp32-cyd-stock-ticker
- **dcluomax/stock-ticker-esp32c6** — MIT License. Reference for setup flow,
  physical-button interaction, and intraday display behavior.
  https://github.com/dcluomax/stock-ticker-esp32c6
- **Xinyuan-LilyGO/T-Display-S3** — MIT License. Authoritative board pinout,
  display setup, firmware examples, and PlatformIO reference.
  https://github.com/Xinyuan-LilyGO/T-Display-S3

## Direct dependencies

- **Bodmer/TFT_eSPI** — mixed upstream licensing: MIT-derived Adafruit ILI9341
  portions, BSD-derived Adafruit_GFX portions, and FreeBSD for Bodmer's original
  code as described in the project's `license.txt`.
  https://github.com/Bodmer/TFT_eSPI
- **Bodmer/U8g2_for_TFT_eSPI** — version 1.7.0 on the upstream `master` branch;
  adapted from U8g2_for_Adafruit_GFX/U8g2. The inherited U8g2 code uses a
  BSD 2-Clause-style license; retain the upstream notices when redistributing.
  https://github.com/Bodmer/U8g2_for_TFT_eSPI
  https://github.com/olikraus/u8g2/blob/master/LICENSE
- **tzapu/WiFiManager** — MIT License.
  https://github.com/tzapu/WiFiManager
- **bblanchon/ArduinoJson** — MIT License.
  https://github.com/bblanchon/ArduinoJson

Each dependency remains subject to its own upstream license and notices.
