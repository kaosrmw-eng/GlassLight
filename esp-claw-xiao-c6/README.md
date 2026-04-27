# ESP-Claw — XIAO ESP32-C6 Board Port

Board support files for running [ESP-Claw](https://github.com/espressif/esp-claw) on the **Seeed Studio XIAO ESP32-C6**.

## Hardware

| Item | Detail |
|------|--------|
| Chip | ESP32-C6 (dual-core RISC-V, 160MHz HP + 20MHz LP) |
| Flash | 4MB |
| PSRAM | None |
| Wi-Fi | 6 (802.11ax) |
| BLE | 5.0 |
| Protocol | Zigbee / Thread / Matter |
| LED | WS2812 RGB on GPIO8 |

## Files

| File | Purpose |
|------|---------|
| `board_info.yaml` | Chip target declaration (`esp32c6`) |
| `board_peripherals.yaml` | RMT config for GPIO8 WS2812 (no DMA — C6 limitation) |
| `board_devices.yaml` | LED strip device definition |
| `setup_device.c` | Board initialization stub |
| `sdkconfig.defaults.board` | C6-specific config: no SPIRAM, 4MB flash, trimmed buffers |
| `partitions_4MB_c6.csv` | Single OTA slot partition table for 4MB flash |

## Key Differences from S3 Reference Board

- **No PSRAM** — ESP-Claw's task allocator detects this at runtime and falls back to internal SRAM automatically. No code changes needed.
- **4MB Flash** — Single OTA slot (no OTA-1). 512K SPIFFS for emote + 512K FAT for skills/scripts.
- **RMT no DMA** — C6's RMT peripheral doesn't support DMA. `with_dma: false` in peripherals config.
- **GPIO8** — XIAO C6 onboard WS2812 is on GPIO8 (vs GPIO38 on S3 DevKitC).
- **160MHz CPU** — C6 max is 160MHz vs 240MHz on S3. Adequate for agent loop + Telegram IM.

## Build Instructions

### Prerequisites
- ESP-IDF v5.5.4+
- `pip install esp-bmgr-assist`

### Steps

```bash
# Clone ESP-Claw
git clone https://github.com/espressif/esp-claw.git
cd esp-claw/application/basic_demo

# Copy this board directory into boards/
cp -r /path/to/this/folder ./boards/xiao_esp32c6

# Copy partition table
cp /path/to/partitions_4MB_c6.csv ./

# Generate board config
idf.py gen-bmgr-config -c ./boards -b xiao_esp32c6

# Set IDF target
idf.py set-target esp32c6

# Configure (set WiFi, LLM key, Telegram token)
idf.py menuconfig

# Build + flash
idf.py build
idf.py flash monitor
```

## Recommended Configuration (menuconfig)

- **LLM**: Claude 3.5 Sonnet or DeepSeek V3 (strong tool-use, lower token cost)
- **IM**: Telegram (lightest, most reliable on constrained hardware)
- **Disable**: QQ, Feishu, WeChat (save RAM)
- **Memory module**: Optional — disable if RAM is tight
- **Web search**: Optional — enable if you have a Brave/Tavily key

## Status

- [ ] Build verified
- [ ] Flash verified  
- [ ] Telegram IM working
- [ ] Agent loop working
- [ ] MCP client working

*First attempt — community contributions welcome.*
