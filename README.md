# GlassLight

An open-source wearable smart light designed to clip onto Meta Rayban 2 glasses. Built on the Seeed Studio XIAO ESP32-C6 with a single WS2812B LED.

![GlassLight](https://img.shields.io/badge/platform-ESP32--C6-blue) ![License](https://img.shields.io/badge/license-MIT-green) ![Version](https://img.shields.io/badge/version-1.6-orange)

## Features

- **7-color palette** — cycle forward/back with physical buttons
- **WiFi config portal** — hold a button 3 seconds, connect your phone, configure everything
- **Live control page** — change color and brightness from your phone over WiFi
- **Persistent notepad** — up to 2MB of notes stored in SPIFFS, survives reboots
- **Battery monitoring** — ADC voltage divider, low/critical warnings via LED
- **Auto-off timer** — configurable idle shutoff
- **Light sleep** — drops to ~3mA idle when WiFi is off
- **3D printable enclosure** — STL files coming soon

## Hardware

| Component | Details |
|---|---|
| Board | Seeed Studio XIAO ESP32-C6 |
| LED | 1× WS2812B |
| Buttons | 3× tactile (next color, prev color, toggle/config) |
| Power | LiPo battery via XIAO onboard charger |

### Pin Map

| Signal | GPIO | XIAO Label |
|---|---|---|
| WS2812B Data | 18 | D10 |
| Button — Next Color | 20 | D9 |
| Button — Prev Color | 19 | D8 |
| Button — Toggle / Config | 17 | D7 |
| Battery ADC | 0 | D0 |

### Battery Sense Wiring

A resistor voltage divider brings the LiPo voltage into ADC range:

```
BAT+ ──[ 1kΩ ]──┬── D0 (GPIO0)
                │
             [100Ω]
                │
               GND
```

Divider ratio: 100 / (1000 + 100) = 0.0909  
LiPo full ~4.2V → ADC ~474 | LiPo empty ~3.0V → ADC ~339

> **Calibration:** Open Serial Monitor at 115200 baud and send `r` to read the raw ADC value. Update `BAT_ADC_FULL` and `BAT_ADC_EMPTY` in the firmware to match your actual readings.

## Button Reference

| Button | Action |
|---|---|
| D9 (Next) | Cycle to next active color |
| D8 (Prev) | Cycle to previous active color |
| D7 (Toggle) — tap | Turn LED on / off |
| D7 (Toggle) — hold 3s | Enter WiFi config mode (3× blue blink) |

## WiFi Config Mode

Connect to `GlassLight-Setup` (open network). The captive portal opens automatically on most phones.

| URL | Page |
|---|---|
| `192.168.4.1/` | Settings (boot color, brightness, auto-off, active colors) |
| `192.168.4.1/control` | Live LED control |
| `192.168.4.1/notes` | Persistent notepad (paste, edit, copy, clear) |

## Arduino Setup

### Library Required

Install via Arduino Library Manager:
- **Adafruit NeoPixel** by Adafruit

All other libraries (`WiFi`, `WebServer`, `DNSServer`, `Preferences`, `SPIFFS`) are included in the ESP32 Arduino core.

### Board Settings

| Setting | Value |
|---|---|
| Board | `XIAO_ESP32C6` (Tools → Board → ESP32 Arduino) |
| Partition Scheme | `Default 4MB with spiffs` ← **required for notepad** |

## Battery Warnings

| Level | Behavior |
|---|---|
| ≤ 20% | 3 red flashes every 60 seconds |
| ≤ 10% | Continuous slow red pulse (LED taken over) |

## 3D Enclosure

STL files for the glasses-clip enclosure coming soon.
Arch.stl - Connects both Batter & Esp32 enclosures and slides on left arm of the Meta RayBan 2 glasses.
Batt_LED_BoxA - Enclosre for battery and LED 
Batt_LED_BoxB - " lower half
Ctl_BoxA - Enclosure for Esp32 and buttons/switch
Ctl_BoxB - " lower half

## License

MIT — do whatever you want with it. Attribution appreciated but not required.

## Author

Rich Washburn — [richwashburn.com](https://richwashburn.com)
