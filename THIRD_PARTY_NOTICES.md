# Third-Party Notices

This project is implemented as a modular T-Display-S3 application. The following open-source projects are used as libraries, hardware references, or design/code references.

## Design and hardware references

- **Zaitronics/esp32-cyd-stock-ticker** — MIT License. Reference for ESP32 stock ticker configuration flow, WiFiManager usage, and local web configuration.
  https://github.com/Zaitronics/esp32-cyd-stock-ticker
- **dcluomax/stock-ticker-esp32c6** — MIT License. Reference for setup flow, physical-button interaction, and intraday display behavior.
  https://github.com/dcluomax/stock-ticker-esp32c6
- **Xinyuan-LilyGO/T-Display-S3** — MIT License. Authoritative board pinout, display setup, firmware examples, and PlatformIO reference.
  https://github.com/Xinyuan-LilyGO/T-Display-S3
- **Keralots/BambuHelper** — MIT License. Pinned reference during the Bambu MQTT runtime alignment at commit `d7a898394c046495798d87e50afd91ecf63f6ce7`. This firmware adapts BambuHelper's Cloud MQTT connection lifecycle: fresh TLS/MQTT client recreation on reconnect, strict-CA `WiFiClientSecure` setup, PubSubClient-owned Cloud connection, randomized `bblp_*` client IDs, 30/60/120-second failure backoff, loop-driven scheduling, watchdog-reset placement around long operations, and delayed initial `pushall`. The T-Display GP keeps its own Manual Token configuration, local active-printer selection, read-only state model and UI; BambuHelper light/camera/power/Tasmota/control paths are not imported.
  https://github.com/Keralots/BambuHelper

## Direct dependencies

- **Bodmer/TFT_eSPI** — mixed upstream licensing: MIT-derived Adafruit ILI9341 portions, BSD-derived Adafruit_GFX portions, and FreeBSD for Bodmer's original code as described in the project's `license.txt`.
  https://github.com/Bodmer/TFT_eSPI
- **Bodmer/U8g2_for_TFT_eSPI** — version 1.7.0 on the upstream `master` branch; adapted from U8g2_for_Adafruit_GFX/U8g2. The inherited U8g2 code uses a BSD 2-Clause-style license; retain upstream notices when redistributing.
  https://github.com/Bodmer/U8g2_for_TFT_eSPI
  https://github.com/olikraus/u8g2/blob/master/LICENSE
- **tzapu/WiFiManager** — MIT License.
  https://github.com/tzapu/WiFiManager
- **bblanchon/ArduinoJson** — MIT License.
  https://github.com/bblanchon/ArduinoJson
- **knolleary/PubSubClient** — MIT License. Version 2.8 is used for persistent verified-TLS Bambu Cloud MQTT.
  https://github.com/knolleary/pubsubclient

Each dependency remains subject to its own upstream license and notices.
