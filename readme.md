# Hyperk

Hyperk is a minimalist, high-performance uni-platform WiFi/Ethernet LED driver for ESP8266, ESP32 (S2, S3, C2, C3, C5, C6), Raspberry Pi Pico W (RP2040, RP2350). Designed as a lightweight and streamlined component that avoids unnecessary complexity, it delivers low‑latency performance and integrates smoothly with platforms such as HyperHDR, while offering essential home‑automation capabilities through a clean, modern codebase.

## Installation

The firmware can be flashed directly from your browser:
**[hyperk.hyperhdr.org](https://hyperk.hyperhdr.org)**

> [!TIP]
> Once installed, you can also perform OTA updates directly through the local **Web GUI**.

---

## Supported Hardware

- **Espressif:** ESP8266, ESP32, ESP32-S2, ESP32-S3, ESP32-C2, ESP32-C3, ESP32-C5, ESP32-C6, WT32-ETH01
   - Initial support for custom boards: GLEDOPTO, DOMRAEM, Athom/IoTorero including LAN8720 chipset  
   
- **Raspberry Pi Pico W:** RP2040, RP2350

Includes support for multi-segment (board-dependent), power-relay control, and HyperSerial (USB serial port communication).

## Supported LED Types

- **NeoPixel RGB:** WS2812b and compatible.
- **NeoPixel RGBW:** SK6812 (includes white channel calibration known from HyperSerial).
- **DotStar SPI:** APA102 and high-speed clocked LEDs.

## Manual

👉 [https://wiki.hyperhdr.eu/Hyperk](https://wiki.hyperhdr.eu/Hyperk)

## Integration

- **HyperHDR WiFi:** Dedicated Hyperk driver. Also works via its native `DDP`, `udpraw`, and `WLED` drivers.
- **HyperHDR Serial Port:** HyperHDR Adalight USB serial communication variant (AWA protocol).
- **Home Assistant:** Automatic discovery with support for power on/off, color, and brightness control.

|      HyperHDR      |   Home Assistant   |
|--------------------|--------------------|
| [![1](https://github.com/user-attachments/assets/ab252845-50da-4985-96be-d1da7bfd522d)](https://github.com/user-attachments/assets/ab252845-50da-4985-96be-d1da7bfd522d) | [![2a](https://github.com/user-attachments/assets/b3097b11-249a-4a66-9cff-bcdd70f28e87)](https://github.com/user-attachments/assets/b3097b11-249a-4a66-9cff-bcdd70f28e87) |


## Network Services

| Service | Port | Protocol / Description |
| :--- | :--- | :--- |
| Web GUI | 80 | Device configuration |
| UDP DDP | 4048 | DDP listener |
| UDP RealTime | 21324 | Real-time stream listener |
| UDP Raw RGB | 5568 | Raw color stream listener |
| Daylight UI | 8080 | Location based day/night control (this fork) |
  
<small>LEDs turn off automatically 6.5s after stream loss.</small>

## Daylight control (this fork)

Hyperk can keep the LEDs off while it is light outside. The device gets the time via NTP, calculates sunrise and sunset for your location and discards the HyperHDR stream during daylight, so the backend switches the LEDs off (stream timeout). At night everything works exactly as before.

- Open `http://hyperk.local:8080/` (or `http://<device-ip>:8080/`). The page uses the same look as the main GUI.
- Set your location by searching a place name, by pasting coordinates copied from Google Maps (e.g. `48.137154, 11.576124`, a Maps link also works) or by typing latitude/longitude.
- Choose when it counts as "dark" (sunset, civil twilight = default, nautical, astronomical) and optional offsets in minutes.
- Enable the rule and save. The status card shows sunrise/sunset in your local time and whether the stream is currently allowed or blocked. Manual override (always allow / always block) is available for testing.
- The page also has a **Firmware update** card that takes a `.bin` file directly, which the stock GUI does not offer.
- API: `GET /api/daylight` returns the status as JSON, add `?at=<unix epoch>` to simulate a moment in time. `POST /api/daylight` takes the same fields as plain form parameters. `GET /api/ping` answers `ok` and is handy to check that the device responds at all.

Notes: set the static color in the main GUI to black so the LEDs are really off during daylight. Without a time sync or without a location the LEDs behave as usual (fail-open). The gate only applies to the network stream (USB serial and Home Assistant are not affected). Available on ESP8266 and ESP32 builds with the async web server; other boards behave as before.

### Installing this firmware the first time

The stock GUI on port 80 only installs updates from the project's own release server, it has no file picker. The device does accept an upload though, so send the file to its `/ota` endpoint once. Replace the address with your device:

```
curl -F "update=@OTA_Hyperk_0.0.5_esp8266.bin;filename=firmware.bin" \
     -H "hyperk-ota-firmware-name: OTA_Hyperk_0.0.5_esp8266.bin" \
     -H "hyperk-ota-firmware-size: 488288" \
     http://hyperk.local/ota
```

The file name has to contain the board name (`esp8266`) and the size has to match the file, otherwise the device rejects it. Flashing over USB with esptool or the web flasher works as well.

From then on use the **Firmware update** card on `http://hyperk.local:8080/`: choose a `.bin` file, press upload, done.

Ready-made firmware: change the tag name in `.github/daylight-release-tag` (e.g. `daylight-v2`) and push, or push a tag `daylight-v*`, or use **Actions → Release Daylight Firmware → Run workflow**. The workflow builds the ESP8266 firmware and publishes it under **Releases**. Upload `OTA_Hyperk_<version>_esp8266.bin` in the OTA section of the Hyperk GUI.

---
*Developed for performance. Optimized for HyperHDR. [Privacy & Technical Note](https://awawa-dev.github.io/hyperk/privacy.html)*
