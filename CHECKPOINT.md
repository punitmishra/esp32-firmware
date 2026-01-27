# RailCommand ESP32 Project Checkpoint

**Date:** 2026-01-26
**Status:** Working - 3-Node Multi-ESP32 System with real-time train, news, and weather updates

---

## Project Overview

A model railroad control system using **3 ESP32-C3 microcontrollers** with OLED displays, connected to a Python Flask API server via WebSocket for real-time train status updates, live news from RSS feeds, and weather data. All nodes receive synchronized updates simultaneously.

---

## Current State

### Multi-Node Setup (3 Nodes Online)

| Node | IP Address | Device Name | Status |
|------|------------|-------------|--------|
| Node-1 | 192.168.0.251 | RailCmd-Node-1 | Online |
| Node-2 | 192.168.0.252 | RailCmd-Node-2 | Online |
| Node-3 | 192.168.0.253 | RailCmd-Node-3 | Online |

### What's Working
- **3 ESP32-C3 nodes** with synchronized displays
- ESP32-C3 firmware with 7 display screens (WiFi, BLE, Trains, Schedule, Stats, News, Weather)
- OLED display using U8G2 library (software I2C on GPIO5/6)
- **Static IP assignment** per node (251, 252, 253)
- WiFi AP mode for configuration + Station mode for network connection
- BLE for mobile configuration
- **WebSocket server on each ESP32** (ESPAsyncWebServer on port 81)
- **Python connects as WebSocket client** to all 3 ESP32s for real-time push updates
- HTTP polling as fallback (trains, schedule, news, weather - every 10 seconds)
- HTTP status reporting to API
- Web interface on ESP32 for configuration
- API server with train control, schedule, news, weather, and node management
- Dashboard with train controls at http://192.168.0.177:5000
- Live news headlines from BBC RSS feed (auto-refreshes every 5 minutes)
- Live weather data from wttr.in API (auto-refreshes every 5 minutes)
- Animated boot screen with train and smoke puffs (positioned at Y+26)
- Screen transitions with icons showing next screen name
- Smooth scrolling for long news headlines

### Architecture
The WebSocket connection uses an inverted architecture with multi-node support:
- **Each ESP32 runs WebSocket SERVER** on port 81 at `/ws` endpoint using ESPAsyncWebServer
- **Python Flask server connects as WebSocket CLIENT** to all 3 ESP32s:
  - `ws://192.168.0.251:81/ws` (Node-1)
  - `ws://192.168.0.252:81/ws` (Node-2)
  - `ws://192.168.0.253:81/ws` (Node-3)
- Updates are broadcast to all connected nodes simultaneously
- Each node has its own connection thread for reliability

### Known Issues
- None currently - All 3 nodes connected and synchronized

### Hardware Setup
- **Board:** ESP32-C3 SuperMini (x3)
- **Display:** SSD1306 OLED 128x64 (72x40 visible area)
  - Visible area offset: X=30, Y=12
  - Content must be drawn within X:30-102, Y:12-52
  - Center X = 66 (X + 36)
- **I2C Pins:** SDA=GPIO5, SCL=GPIO6
- **LED:** GPIO8
- **Button:** GPIO9 (BOOT button for screen cycling)

### Network
| Device | IP Address |
|--------|------------|
| Node-1 | 192.168.0.251 |
| Node-2 | 192.168.0.252 |
| Node-3 | 192.168.0.253 |
| API Server | 192.168.0.177:5000 |

**WiFi AP SSIDs for configuration:**
- Node-1: `RailCmd-Node-1` (password: 12345678)
- Node-2: `RailCmd-Node-2` (password: 12345678)
- Node-3: `RailCmd-Node-3` (password: 12345678)

---

## File Locations

### ESP32 Firmware
```
/Users/punitmishra/Downloads/esp32-firmware/
├── platformio.ini          # PlatformIO config with U8G2 & WebSockets
├── src/
│   └── main.cpp            # Main firmware code
└── CHECKPOINT.md           # This file
```

### API Server
```
/Users/punitmishra/Downloads/railcommand-api/
├── app.py                  # Flask API with WebSocket support
└── requirements.txt        # Python dependencies
```

### TypeScript API Client (Reference)
```
/Users/punitmishra/Downloads/railroad-api.ts   # API type definitions
```

---

## How to Run

### 1. Start API Server
```bash
cd /Users/punitmishra/Downloads/railcommand-api
pip3 install -r requirements.txt  # First time only
python3 app.py
```

### 2. Build & Upload ESP32 Firmware (Multi-Node)
```bash
cd /Users/punitmishra/Downloads/esp32-firmware

# Upload to Node-1 (IP: 192.168.0.251)
python3 -m platformio run -t upload -e node1

# Upload to Node-2 (IP: 192.168.0.252)
python3 -m platformio run -t upload -e node2

# Upload to Node-3 (IP: 192.168.0.253)
python3 -m platformio run -t upload -e node3
```

### 3. Configure WiFi on New Nodes
1. Connect to AP: `RailCmd-Node-X` (password: `12345678`)
2. Open http://192.168.4.1
3. Enter WiFi credentials and save
4. Device reboots with static IP

### 4. Monitor Serial Output
```bash
python3 -m platformio device monitor
```

---

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Dashboard with train controls |
| `/ws` | WebSocket | Plain WebSocket for ESP32 |
| `/api/trains` | GET | Get all trains |
| `/api/trains/<idx>` | POST | Control train (speed, direction) |
| `/api/trains/all/stop` | POST | Emergency stop all |
| `/api/schedule` | GET | Get schedule |
| `/api/news` | GET/POST | Get or update news headlines |
| `/api/weather` | GET/POST | Get or update weather data |
| `/api/esp32/status` | POST | ESP32 status updates |
| `/api/nodes` | GET | Get connected ESP32 nodes |

### Example API Calls
```bash
# Get trains
curl http://192.168.0.177:5000/api/trains

# Start train at 50% forward
curl -X POST -H "Content-Type: application/json" \
  -d '{"speed": 50, "direction": "forward"}' \
  http://192.168.0.177:5000/api/trains/0

# Emergency stop
curl -X POST http://192.168.0.177:5000/api/trains/all/stop

# Get news headlines
curl http://192.168.0.177:5000/api/news

# Get weather
curl http://192.168.0.177:5000/api/weather

# Manually set weather (if API is down)
curl -X POST -H "Content-Type: application/json" \
  -d '{"condition": "sunny", "temp": 22, "humidity": 45, "location": "Home"}' \
  http://192.168.0.177:5000/api/weather

# Check ESP32 status
curl http://192.168.0.253/api/status

# Get connected ESP32 nodes
curl http://192.168.0.177:5000/api/nodes
```

---

## WebSocket Message Format

**Connection Flow:**
1. ESP32 starts WebSocket server on port 81
2. Python connects as client to `ws://192.168.0.253:81/ws`
3. ESP32 sends `register` message with device info
4. Python responds with initial train/schedule/news/weather data
5. Python pushes updates whenever data changes

### Python → ESP32 (via WebSocket client)

**Train Update:**
```json
{
  "type": "train_update",
  "trains": [
    {"name": "Express 1", "speed": 50, "direction": "forward", "active": true}
  ]
}
```

**Schedule Update:**
```json
{
  "type": "schedule_update",
  "schedule": [
    {"time": "10:30", "train": "Express", "dest": "Central", "platform": 1}
  ]
}
```

**News Update:**
```json
{
  "type": "news_update",
  "news": [
    {"headline": "Breaking news headline here", "source": "BBC News"}
  ]
}
```

**Weather Update:**
```json
{
  "type": "weather_update",
  "condition": "clear",
  "temp": 9,
  "humidity": 80,
  "location": "San Jose"
}
```

**Commands:**
```json
{"type": "command", "cmd": "reboot"}
{"type": "command", "cmd": "screen", "screen": 2}
```

### ESP32 → Server

**Registration:**
```json
{"type": "register", "device": "ESP32-C3-F610", "ip": "192.168.0.253"}
```

---

## Display Screens (Auto-cycles, button to skip)

| Screen | Duration | Content |
|--------|----------|---------|
| 1. WiFi | 4 sec | Network status, IP address, online/offline |
| 2. BLE | 4 sec | Bluetooth status, device name, paired count |
| 3. Trains | 4 sec | Active trains with direction (>>/<<) and speed % |
| 4. Schedule | 4 sec | Departure time, destination, platform |
| 5. Stats | 4 sec | Memory usage % and uptime |
| 6. News | 6 sec | BBC headlines with smooth scrolling |
| 7. Weather | 6 sec | Temperature, humidity, condition with icon |

**Features:**
- Animated train boot screen with smoke puffs and decorative lines
- Clean WiFi connecting animation with progress bar
- Slide-in transition animations showing icon + name
- All icons properly centered using `X + 36 - ICON_W/2` formula
- Consistent layout: icon at Y+12, title at Y+30, content at Y+38/Y+46
- Smooth horizontal scrolling for long news headlines
- Weather icons (sun/cloud) based on conditions
- News/Weather screens stay longer (6 sec) for readability

**Screen Positioning (Y offsets from visible area top):**
- Y+12: Icon position (12px margin from top of visible area)
- Y+30: Title text baseline
- Y+38: Content line 1 baseline
- Y+46: Content line 2 baseline

**Visible Area Coordinates:**
- Display: 128x64 pixels total
- Visible window: 72x40 pixels
- X offset: 30 (visible X range: 30-101)
- Y offset: 12 (visible Y range: 12-51)
- Center X: X + 36 = 66

**Custom Icons (XBM format):**
- WiFi: 12x10 - signal arcs
- Bluetooth: 8x10 - classic B symbol
- Train: 14x9 - locomotive with wheels
- Clock: 10x10 - round clock with hands
- Stats: 10x9 - bar chart with trend
- News: 10x9 - newspaper with lines
- Sun: 11x11 - sun with rays
- Cloud: 12x8 - fluffy cloud

---

## Configuration

### ESP32 WiFi Setup (Per Node)
1. Connect to AP: `RailCmd-Node-X` (password: `12345678`)
   - Node-1: `RailCmd-Node-1`
   - Node-2: `RailCmd-Node-2`
   - Node-3: `RailCmd-Node-3`
2. Open http://192.168.4.1
3. Enter your WiFi credentials and save
4. Device reboots and connects with static IP

### Node Configuration (platformio.ini)
Each node is configured via build environment:
```ini
[env:node1]  # IP: 192.168.0.251
build_flags = ${env.build_flags} -DNODE_ID=1

[env:node2]  # IP: 192.168.0.252
build_flags = ${env.build_flags} -DNODE_ID=2

[env:node3]  # IP: 192.168.0.253
build_flags = ${env.build_flags} -DNODE_ID=3
```

### Change API Server IP
Edit `/Users/punitmishra/Downloads/esp32-firmware/src/main.cpp`:
```cpp
#define API_HOST "192.168.0.177"  // Change to your server IP
#define API_PORT 5000
```

### Add More Nodes
1. Edit `platformio.ini` - add new `[env:nodeX]` with `NODE_ID=X`
2. Edit `src/main.cpp` - add new `#elif NODE_ID == X` block with IP
3. Edit `app.py` - add new entry to `ESP32_NODES` list

---

## Key Code Sections

### main.cpp Structure (~1100 lines)
- Lines 1-35: Includes and configuration constants
- Lines 36-130: Custom XBM icons (WiFi, BLE, Train, Clock, Stats, News, Sun, Cloud)
- Lines 132-185: U8G2 display, data structures (Train, Schedule, News, Weather)
- Lines 187-210: State variables and timing constants
- Lines 212-230: Drawing helpers (signal bars, progress bar)
- Lines 232-275: BLE callbacks
- Lines 277-390: WebSocket event handling and message parsing
- Lines 392-435: API communication functions
- Lines 437-565: Setup functions (OLED boot animation, WiFi connecting screens)
- Lines 567-745: BLE setup, credentials, web server
- Lines 747-810: Animation helpers (trains, dots, speed bars, scrolling text)
- Lines 812-1020: Display screen rendering (all 7 screens + centered transitions)
- Lines 1022-1105: Main setup() and loop() with auto-cycling logic

### app.py Structure (~607 lines)
- Lines 28-36: RSS feed configuration
- Lines 38-95: Background updater thread (news from BBC, weather from wttr.in)
- Lines 101-116: Demo train/schedule data
- Lines 121-231: Plain WebSocket handler for ESP32
- Lines 236-341: Train control and news/weather API endpoints
- Lines 347-402: ESP32 node management API
- Lines 407-552: Dashboard HTML template
- Lines 557-589: Socket.IO events for browser clients

---

## Data Sources

### News
- **Source:** BBC World News RSS Feed
- **URL:** `https://feeds.bbci.co.uk/news/world/rss.xml`
- **Refresh:** Every 5 minutes (background thread)
- **Display:** Top 3 headlines, cycles every 4 seconds, smooth scroll for long text

### Weather
- **Source:** wttr.in API
- **URL:** `https://wttr.in/?format=j1`
- **Refresh:** Every 5 minutes (background thread)
- **Display:** Temperature, humidity, condition with sun/cloud icon

---

## Future Work / Ideas

- [ ] Add actual train control hardware (motor drivers, PWM)
- [ ] Reed switch integration for position detection
- [ ] BMP280 sensor for temperature/pressure display
- [x] **Multiple ESP32 nodes at different locations** ✓ (3 nodes working!)
- [ ] Mobile app using BLE for local control
- [ ] Schedule automation (timed train runs)
- [ ] Camera integration for monitoring
- [ ] Sound effects via API
- [ ] OTA firmware updates

---

## Troubleshooting

### Display not working
- Check I2C wiring: SDA→GPIO5, SCL→GPIO6
- Verify 3.3V power to display
- Software I2C is used (no Wire.begin needed)

### WebSocket not connecting
- Verify API server is running: `curl http://192.168.0.177:5000/api/trains`
- Check ESP32 is on same network: `curl http://192.168.0.253/api/status`
- Verify API_HOST in main.cpp matches server IP

### Build errors
- Run `pio pkg update` to update libraries
- Clean build: `pio run -t clean && pio run`

---

## Session Commands Used

```bash
# Build and upload firmware (multi-node)
python3 -m platformio run -t upload -e node1  # Node-1 @ 192.168.0.251
python3 -m platformio run -t upload -e node2  # Node-2 @ 192.168.0.252
python3 -m platformio run -t upload -e node3  # Node-3 @ 192.168.0.253

# Start API server (runs on port 5000)
cd /Users/punitmishra/Downloads/railcommand-api && python3 app.py

# Start API server in background
cd /Users/punitmishra/Downloads/railcommand-api && nohup python3 app.py > /tmp/railcommand.log 2>&1 &

# Monitor serial output
python3 -m platformio device monitor

# Test API endpoints
curl http://192.168.0.177:5000/api/trains
curl http://192.168.0.177:5000/api/news
curl http://192.168.0.177:5000/api/weather
curl http://192.168.0.177:5000/api/nodes

# Check individual nodes
curl http://192.168.0.251/api/status  # Node-1
curl http://192.168.0.252/api/status  # Node-2
curl http://192.168.0.253/api/status  # Node-3

# View API server logs
tail -f /tmp/railcommand.log
```
