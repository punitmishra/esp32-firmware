# RailCommand ESP32 Project Checkpoint

**Date:** 2026-11-25
**Status:** 3-node ESP32-C3 system — WiFi/OLED/BLE working, house LED drivers added, API_HOST now points at the Pi (Rust `train-controller` on :5000)

---

## Project Overview

A model railroad control system where 3 ESP32-C3 SuperMini boards act as
scenery zone controllers:
- Each board has an SSD1306 OLED showing live train / schedule / weather / news
- Each board drives 6 building LEDs (houses, beacons, signs) via 2N3904 drivers
- All three boards speak to a Raspberry Pi running the Rust
  `train-controller` API (see `github.com/punitmishra/pi-railroad-control`)

The Pi is the brain; the ESP32s are the scenery edge.

---

## Current state

### Nodes

| Node | IP | Device name | Zone | Role |
|------|----|--|---|---|
| Node-1 | 192.168.0.251 | RailCmd-Node-1 | Downtown / Station | Station hall, Platform lamp, Ticket booth, Clock tower, Cafe window, Billboard |
| Node-2 | 192.168.0.252 | RailCmd-Node-2 | Upper Level / Construction | Fire-station bay, Site floodlight, Crane warning, Site office, Emergency beacon, Barricade LED |
| Node-3 | 192.168.0.253 | RailCmd-Node-3 | Lower Level / Mansion | Mansion porch, Mansion window, Police station, Police beacon, Coffee-shop sign, Garden lamp |

### What's working
- 3 ESP32-C3 nodes with static IPs and OLED displays
- WiFi (AP+STA), BLE config, 7 display screens, animated boot
- WebSocket server on each ESP32 (port 81, `/ws`) — Pi connects as client
- HTTP server on each (port 80) — see endpoints below
- HTTP polling fallback for trains/schedule/weather/news every ~10 s
- Status heartbeat (POST `/api/esp32/status`) every 5 s
- **NEW:** 6 house LED driver pins per node (GPIO 0/1/3/4/7/10), each
  switches a 2N3904 → LED to light a scenery building

### Hardware setup

| Signal | GPIO | Notes |
|--------|------|-------|
| OLED SDA | 5 | Software I²C to SSD1306 |
| OLED SCL | 6 | |
| Status LED | 8 | On-board, reserved |
| BOOT button | 9 | Short = cycle screen, long = reset WiFi |
| House LED 0 | 0 | 2N3904 low-side driver → scenery LED |
| House LED 1 | 1 | ditto |
| House LED 2 | 3 | |
| House LED 3 | 4 | |
| House LED 4 | 7 | |
| House LED 5 | 10 | |

Each driver: GPIO → 1 kΩ → 2N3904 base; LED anode → 220 Ω → node 3V3 pin;
LED cathode → 2N3904 collector; emitter → GND. GPIO HIGH turns the LED on.
All six emitters bus to the node GND, which **must** tie back to the
main system ground (Pi Pin 34 / 12 V−) or the "off" state will glow.

Each node pulls ~110 mA @ 5 V with all LEDs on — well under a USB-C
charger's budget.

### Display — SSD1306 128×64 (72×40 visible)
- Visible area offset: X=30, Y=12
- Content must stay within X:30-102, Y:12-52
- Center X = 66

---

## API

### Node-side HTTP (each node, port 80)

| Route | Method | Purpose |
|-------|--------|---------|
| `/` | GET | Config landing page |
| `/save` | POST | Save WiFi credentials, reboots |
| `/api/status` | GET | Device info + LED state array |
| `/api/trains` | GET | Last-fetched train data (cached) |
| `/api/led` | GET | Returns `{"leds":[bool;6]}` |
| `/api/led/<n>/<on\|off>` | POST | Drive one building LED |
| `/reboot` | GET | Reboot this node |

### Node-side WebSocket (port 81, `/ws`)

Server endpoint; the Pi connects as the client. Messages are JSON with
a `cmd` field:
- `set_screen` — jump to a specific screen (0–6)
- `train_update`, `weather_update`, `news_update`, `schedule_update` —
  push a slice of state to this node without a round-trip HTTP fetch

### Pi API endpoints the firmware uses

Running on the Pi at `http://192.168.0.165:5000` (change `API_HOST` in
`src/main.cpp` if the Pi's IP moves — put a DHCP reservation on the
router). Each is exposed by the Rust `train-controller`:

| Pi route | Method | Firmware consumer |
|---|---|---|
| `/api/esp32/status` | POST | heartbeat ingest |
| `/api/trains` | GET | `fetchTrainsHTTP()` |
| `/api/schedule` | GET | `fetchScheduleHTTP()` |
| `/api/weather` | GET | `fetchWeatherHTTP()` |
| `/api/news` | GET | `fetchNewsHTTP()` |

The Pi also pushes commands *to* each node:
| Pi → node | Purpose |
|---|---|
| `POST http://<node-ip>/api/led/<n>/<on\|off>` | drive one building LED |

---

## Build & flash

```bash
# Build and flash each node (USB cable)
cd esp32-firmware
python3 -m platformio run -t upload -e node1   # Node-1 .251
python3 -m platformio run -t upload -e node2   # Node-2 .252
python3 -m platformio run -t upload -e node3   # Node-3 .253

# Then OTA (same WiFi, no cable)
python3 -m platformio run -t upload -e node1_ota
```

Serial monitor: `python3 -m platformio device monitor`

### First-boot WiFi config
1. Connect to the AP `RailCmd-Node-X` (password `12345678`)
2. Open `http://192.168.4.1`
3. Enter SSID + password, save → node reboots with its static IP

---

## Changelog since last checkpoint

- **2026-11-25** — Added house LED drivers. `HOUSE_PINS[6]` on GPIO 0/1/3/4/7/10 initialised LOW in `setup()`. New routes `GET /api/led` and `POST /api/led/<n>/<on|off>` via `server.onNotFound` as a path-param router. `/api/status` now includes `led_count` + `leds` array for the Pi to audit. API_HOST updated from `192.168.0.177` (old Flask stub) to `192.168.0.165` (Pi running the Rust train-controller).

---

## Verification (post-flash)

```bash
# Node online check
for n in 251 252 253; do curl -s http://192.168.0.$n/api/status | python3 -m json.tool | head -12; echo "---"; done

# Drive LED 0 on Node-1 (Station hall) directly
curl -X POST http://192.168.0.251/api/led/0/on
curl -X POST http://192.168.0.251/api/led/0/off

# Drive via the Pi (what the dashboard does)
curl -X POST http://192.168.0.165:5000/api/houses/1/0/on
curl -X POST http://192.168.0.165:5000/api/houses/scene/evening

# Heartbeats arriving at the Pi?
curl http://192.168.0.165:5000/api/nodes | python3 -m json.tool
# → each node's online=true within 15 s of booting
```

---

## Troubleshooting

### Node not joining WiFi
- Long-press BOOT button → clears stored credentials, reverts to AP mode
- Serial monitor during boot shows the connection attempt

### Can't reach Pi API
- Confirm `API_HOST` in `src/main.cpp` matches `hostname -I` on the Pi
- `curl -v http://<pi-ip>:5000/api/status` from your laptop — if it
  returns JSON, the Pi API is up and the node just needs to re-fetch

### Build errors
- `pio pkg update` to refresh libraries
- Clean build: `pio run -t clean && pio run`
- If `server.onNotFound` doesn't compile on an older WebServer: check
  `WebServer` library version in `platformio.ini`

### LED stuck on at boot
- GPIO 0/2/3 are strap pins on ESP32-C3. GPIO 0 and 3 may glow for ~50
  ms during reset — expected, ignore.
- If the LED stays on permanently, double-check your 2N3904 collector
  is on the LED cathode, not the anode.

---

## Related repos

- **Pi controller:** github.com/punitmishra/pi-railroad-control (Rust
  actix-web API, CPX firmware, SVG wiring diagrams, CHECKS.md)
- **This repo:** github.com/punitmishra/esp32-firmware (PlatformIO,
  Arduino framework, ESP32-C3)
