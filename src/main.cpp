#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <Wire.h>
#include <U8g2lib.h>

// ============== CONFIGURATION ==============
// Node ID: Set via build flag -DNODE_ID=1, -DNODE_ID=2, or -DNODE_ID=3
// Defaults to 1 if not set
#ifndef NODE_ID
#define NODE_ID 1
#endif

// Node names and static IPs
#if NODE_ID == 1
  #define NODE_NAME "Node-1"
  #define STATIC_IP_LAST_OCTET 251
#elif NODE_ID == 2
  #define NODE_NAME "Node-2"
  #define STATIC_IP_LAST_OCTET 252
#elif NODE_ID == 3
  #define NODE_NAME "Node-3"
  #define STATIC_IP_LAST_OCTET 253
#else
  #define NODE_NAME "Node-X"
  #define STATIC_IP_LAST_OCTET 250
#endif

#define DEVICE_NAME_PREFIX "RailCmd-"
#define BLE_SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Network Configuration
#define NETWORK_GATEWAY     "192.168.0.1"
#define NETWORK_SUBNET      "255.255.255.0"
#define NETWORK_DNS         "8.8.8.8"

// API Configuration
//   The Pi runs the Rust train-controller on :5000. Set a DHCP reservation
//   on the router so this stays stable, or edit here and reflash.
#define API_HOST          "192.168.0.165"
#define API_PORT          5000
#define API_UPDATE_INTERVAL 5000   // Send updates every 5 seconds
#define WS_RECONNECT_INTERVAL 5000

// ============== HARDWARE ==============
#define OLED_SDA 5
#define OLED_SCL 6
#define LED_PIN 8
#define BOOT_BTN 9

// Building LED outputs — one 2N3904 driver per pin, drives a scenery LED.
// Six pins free after the I²C + onboard LED + BOOT button. Order matters:
// the index here is what /api/led/<n> addresses and must match the Pi API's
// NODES[].buildings order.
#define HOUSE_LED_COUNT 6
const uint8_t HOUSE_PINS[HOUSE_LED_COUNT] = { 0, 1, 3, 4, 7, 10 };
bool houseLedState[HOUSE_LED_COUNT] = { false, false, false, false, false, false };

// OLED offsets for 72x40 visible area
const int X = 30;
const int Y = 12;

// ============== CUSTOM ICONS (XBM format) ==============
// WiFi icon 12x10 - cleaner signal arcs
#define ICON_WIFI_W 12
#define ICON_WIFI_H 10
static const unsigned char icon_wifi[] PROGMEM = {
  0xF8, 0x01, 0x06, 0x06, 0xF1, 0x08, 0x0E, 0x07, 0xE0, 0x00, 0x18, 0x01,
  0xE0, 0x00, 0x40, 0x00, 0x60, 0x00, 0x60, 0x00
};

// Bluetooth icon 8x10 - classic B symbol
#define ICON_BLE_W 8
#define ICON_BLE_H 10
static const unsigned char icon_ble[] PROGMEM = {
  0x0C, 0x0A, 0x09, 0x79, 0x3E, 0x1C, 0x3E, 0x79, 0x0A, 0x0C
};

// Train icon 14x9 - locomotive with wheels
#define ICON_TRAIN_W 14
#define ICON_TRAIN_H 9
static const unsigned char icon_train[] PROGMEM = {
  0xC0, 0x01, 0xF0, 0x03, 0xFC, 0x1F, 0xFE, 0x3F, 0xFF, 0x3F, 0xDB, 0x36,
  0xFF, 0x3F, 0x00, 0x00, 0xCC, 0x19
};

// Clock icon 10x10 - round clock with hands
#define ICON_CLOCK_W 10
#define ICON_CLOCK_H 10
static const unsigned char icon_clock[] PROGMEM = {
  0x78, 0x00, 0x86, 0x01, 0x21, 0x02, 0x11, 0x02, 0x19, 0x02, 0xF1, 0x03,
  0x01, 0x02, 0x02, 0x01, 0x86, 0x01, 0x78, 0x00
};

// Stats icon 10x9 - bar chart with trend
#define ICON_STATS_W 10
#define ICON_STATS_H 9
static const unsigned char icon_stats[] PROGMEM = {
  0x00, 0x02, 0x00, 0x03, 0x60, 0x01, 0x60, 0x00, 0x6C, 0x00, 0x6C, 0x00,
  0x6D, 0x00, 0x6D, 0x00, 0xFF, 0x03
};

// Train moving right 8x5
#define ICON_LOCO_W 8
#define ICON_LOCO_H 5
static const unsigned char icon_loco_r[] PROGMEM = {
  0x7E, 0xFF, 0xDB, 0x7E, 0x24
};

// Train moving left 8x5
static const unsigned char icon_loco_l[] PROGMEM = {
  0x7E, 0xFF, 0xDB, 0x7E, 0x24
};

// Smoke puff 4x3
#define ICON_SMOKE_W 4
#define ICON_SMOKE_H 3
static const unsigned char icon_smoke[] PROGMEM = {
  0x06, 0x0F, 0x06
};

// Large train for boot screen 24x12
#define ICON_BOOT_TRAIN_W 24
#define ICON_BOOT_TRAIN_H 12
static const unsigned char icon_boot_train[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x80, 0xFF, 0x00, 0xC0, 0xFF, 0x01,
  0xF0, 0xFF, 0x07, 0xF8, 0xFF, 0x0F, 0xFC, 0xFF, 0x1F, 0xFC, 0xC3, 0x1F,
  0xFC, 0xFF, 0x1F, 0xF8, 0xFF, 0x0F, 0x00, 0x00, 0x00, 0x98, 0xC3, 0x0C
};

// News icon 10x9 - newspaper with lines
#define ICON_NEWS_W 10
#define ICON_NEWS_H 9
static const unsigned char icon_news[] PROGMEM = {
  0xFF, 0x03, 0x01, 0x02, 0xFD, 0x02, 0x01, 0x02, 0xFD, 0x02, 0x01, 0x02,
  0xF5, 0x02, 0x01, 0x02, 0xFF, 0x03
};

// Weather sun icon 11x11 - sun with rays
#define ICON_SUN_W 11
#define ICON_SUN_H 11
static const unsigned char icon_sun[] PROGMEM = {
  0x20, 0x00, 0x20, 0x00, 0x24, 0x01, 0xF8, 0x00, 0xFC, 0x01, 0xFE, 0x03,
  0xFC, 0x01, 0xF8, 0x00, 0x24, 0x01, 0x20, 0x00, 0x20, 0x00
};

// Weather cloud icon 12x8 - fluffy cloud
#define ICON_CLOUD_W 12
#define ICON_CLOUD_H 8
static const unsigned char icon_cloud[] PROGMEM = {
  0x38, 0x00, 0x44, 0x00, 0xC2, 0x01, 0x01, 0x02, 0x01, 0x04, 0xFF, 0x07,
  0x01, 0x04, 0xFE, 0x03
};

// ============== OBJECTS ==============
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, OLED_SCL, OLED_SDA, U8X8_PIN_NONE);

WebServer server(80);
AsyncWebServer asyncServer(81);  // WebSocket server on port 81
AsyncWebSocket ws("/ws");        // WebSocket endpoint
HTTPClient http;
Preferences prefs;

// ============== FORWARD DECLARATIONS ==============
void saveWiFiCredentials();
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void handleWebSocketMessage(uint8_t* payload, size_t length);
void sendStatusToAPI();
void fetchAllDataHTTP();
void fetchTrainsHTTP();
void fetchWeatherHTTP();
void fetchNewsHTTP();
void fetchScheduleHTTP();
void drawCentered(int y, const char* text, int charWidth);

BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool bleClientConnected = false;
int bleClientCount = 0;

// ============== TRAIN DATA STRUCTURES ==============
struct TrainInfo {
  char name[24];
  int speed;
  char direction[12];  // "forward", "reverse", "stopped"
  bool active;
  unsigned long lastUpdate;
};

struct ScheduleEntry {
  char time[8];
  char train[24];
  char destination[24];
  int platform;
};

// Train data storage
TrainInfo trains[4] = {0};
int trainCount = 0;
ScheduleEntry schedule[4] = {0};
int scheduleCount = 0;

// ============== NEWS & WEATHER DATA ==============
struct NewsItem {
  char headline[64];
  char source[16];
};

struct WeatherInfo {
  char condition[16];  // "sunny", "cloudy", "rainy", etc.
  int tempC;
  int humidity;
  char location[20];
};

NewsItem news[3] = {0};
int newsCount = 0;
WeatherInfo weather = {0};

// ============== STATE ==============
String deviceName;
String apSSID;
String apPassword = "12345678";

String staSSID = "";
String staPassword = "";
bool staConnected = false;
String staIP = "";

bool wsConnected = false;
bool apiConnected = false;

int currentScreen = 0;
const int TOTAL_SCREENS = 7;  // WiFi, BLE, Trains, Schedule, Stats, News, Weather

unsigned long lastButtonPress = 0;
unsigned long bootTime = 0;
unsigned long lastApiUpdate = 0;
unsigned long lastHttpPoll = 0;
#define HTTP_POLL_INTERVAL 10000  // Poll HTTP every 10 seconds when WS not connected
unsigned long lastWsReconnect = 0;
unsigned long lastScreenCycle = 0;
const unsigned long SCREEN_CYCLE_INTERVAL = 4000;  // 4 seconds for most screens
const unsigned long NEWS_WEATHER_INTERVAL = 6000;  // 6 seconds for news/weather
int animFrame = 0;

// ============== DRAWING HELPERS ==============
void drawSignalBars(int x, int y, int level) {
  for (int i = 0; i < 4; i++) {
    int barHeight = (i + 1) * 2;
    int barX = x + i * 3;
    int barY = y + 8 - barHeight;
    if (i < level) {
      u8g2.drawBox(barX, barY, 2, barHeight);
    } else {
      u8g2.drawFrame(barX, barY, 2, barHeight);
    }
  }
}

void drawProgressBar(int x, int y, int width, int percent) {
  u8g2.drawFrame(x, y, width, 5);
  int fillWidth = (width - 2) * percent / 100;
  if (fillWidth > 0) {
    u8g2.drawBox(x + 1, y + 1, fillWidth, 3);
  }
}

// ============== BLE CALLBACKS ==============
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    bleClientConnected = true;
    bleClientCount++;
    digitalWrite(LED_PIN, LOW);
    Serial.println("BLE Client Connected");
  }

  void onDisconnect(BLEServer* pServer) {
    bleClientConnected = false;
    if (bleClientCount > 0) bleClientCount--;
    digitalWrite(LED_PIN, HIGH);
    Serial.println("BLE Client Disconnected");
    pServer->startAdvertising();
  }
};

class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) {
    String value = pChar->getValue().c_str();
    Serial.printf("BLE Received: %s\n", value.c_str());

    if (value.indexOf(':') > 0) {
      int sep = value.indexOf(':');
      staSSID = value.substring(0, sep);
      staPassword = value.substring(sep + 1);
      saveWiFiCredentials();
      pChar->setValue("OK:Saved");
      pChar->notify();
    } else if (value == "STATUS") {
      String status = "WiFi:" + (staConnected ? staIP : "OFF") + ",BLE:" + String(bleClientCount);
      pChar->setValue(status.c_str());
      pChar->notify();
    } else if (value == "REBOOT") {
      pChar->setValue("OK:Rebooting");
      pChar->notify();
      delay(500);
      ESP.restart();
    }
  }
};

// ============== WEBSOCKET SERVER HANDLER ==============
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("[WS] Client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      wsConnected = true;
      // Send registration info to the server
      {
        StaticJsonDocument<256> doc;
        doc["type"] = "register";
        doc["device"] = deviceName;
        doc["ip"] = staIP;
        String msg;
        serializeJson(doc, msg);
        client->text(msg);
      }
      break;

    case WS_EVT_DISCONNECT:
      Serial.printf("[WS] Client #%u disconnected\n", client->id());
      wsConnected = (ws.count() > 0);  // Still connected if other clients exist
      break;

    case WS_EVT_DATA:
      {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
          data[len] = 0;  // Null terminate
          Serial.printf("[WS] Received: %s\n", (char*)data);
          handleWebSocketMessage(data, len);
        }
      }
      break;

    case WS_EVT_PONG:
      Serial.println("[WS] Pong");
      break;

    case WS_EVT_ERROR:
      Serial.printf("[WS] Error #%u: %s\n", client->id(), (char*)data);
      break;
  }
}

void handleWebSocketMessage(uint8_t* payload, size_t length) {
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    Serial.printf("[WS] JSON parse error: %s\n", error.c_str());
    return;
  }

  const char* type = doc["type"];
  if (!type) return;

  if (strcmp(type, "train_update") == 0) {
    // Update train data
    JsonArray trainsArray = doc["trains"].as<JsonArray>();
    trainCount = 0;
    for (JsonObject train : trainsArray) {
      if (trainCount >= 4) break;
      strncpy(trains[trainCount].name, train["name"] | "Unknown", sizeof(trains[trainCount].name) - 1);
      trains[trainCount].speed = train["speed"] | 0;
      strncpy(trains[trainCount].direction, train["direction"] | "stopped", sizeof(trains[trainCount].direction) - 1);
      trains[trainCount].active = train["active"] | false;
      trains[trainCount].lastUpdate = millis();
      trainCount++;
    }
    Serial.printf("[WS] Updated %d trains\n", trainCount);
  }
  else if (strcmp(type, "schedule_update") == 0) {
    // Update schedule data
    JsonArray schedArray = doc["schedule"].as<JsonArray>();
    scheduleCount = 0;
    for (JsonObject entry : schedArray) {
      if (scheduleCount >= 4) break;
      strncpy(schedule[scheduleCount].time, entry["time"] | "", sizeof(schedule[scheduleCount].time) - 1);
      strncpy(schedule[scheduleCount].train, entry["train"] | "", sizeof(schedule[scheduleCount].train) - 1);
      strncpy(schedule[scheduleCount].destination, entry["dest"] | "", sizeof(schedule[scheduleCount].destination) - 1);
      schedule[scheduleCount].platform = entry["platform"] | 1;
      scheduleCount++;
    }
    Serial.printf("[WS] Updated %d schedule entries\n", scheduleCount);
  }
  else if (strcmp(type, "news_update") == 0) {
    // Update news data
    JsonArray newsArray = doc["news"].as<JsonArray>();
    newsCount = 0;
    for (JsonObject item : newsArray) {
      if (newsCount >= 3) break;
      strncpy(news[newsCount].headline, item["headline"] | "", sizeof(news[newsCount].headline) - 1);
      strncpy(news[newsCount].source, item["source"] | "", sizeof(news[newsCount].source) - 1);
      newsCount++;
    }
    Serial.printf("[WS] Updated %d news items\n", newsCount);
  }
  else if (strcmp(type, "weather_update") == 0) {
    // Update weather data
    strncpy(weather.condition, doc["condition"] | "unknown", sizeof(weather.condition) - 1);
    weather.tempC = doc["temp"] | 0;
    weather.humidity = doc["humidity"] | 0;
    strncpy(weather.location, doc["location"] | "", sizeof(weather.location) - 1);
    Serial.println("[WS] Updated weather");
  }
  else if (strcmp(type, "command") == 0) {
    const char* cmd = doc["cmd"];
    if (strcmp(cmd, "reboot") == 0) {
      ESP.restart();
    } else if (strcmp(cmd, "screen") == 0) {
      currentScreen = doc["screen"] | 0;
    }
  }
}

// ============== API FUNCTIONS ==============
void sendStatusToAPI() {
  if (!staConnected) return;

  String url = String("http://") + API_HOST + ":" + API_PORT + "/api/esp32/status";

  StaticJsonDocument<512> doc;
  doc["device"] = deviceName;
  doc["ip"] = staIP;
  doc["uptime"] = (millis() - bootTime) / 1000;
  doc["heap_free"] = ESP.getFreeHeap();
  doc["rssi"] = WiFi.RSSI();
  doc["screen"] = currentScreen;
  doc["ble_clients"] = bleClientCount;
  doc["ws_connected"] = wsConnected;

  String payload;
  serializeJson(doc, payload);

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    apiConnected = true;
    if (httpCode == HTTP_CODE_OK) {
      String response = http.getString();
      // Could parse response for commands
    }
  } else {
    apiConnected = false;
    Serial.printf("[HTTP] Error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

// HTTP polling functions (fallback when WebSocket not working)
void fetchTrainsHTTP() {
  if (!staConnected) return;

  String url = String("http://") + API_HOST + ":" + API_PORT + "/api/trains";
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      JsonArray arr = doc["trains"].as<JsonArray>();
      trainCount = 0;
      for (JsonObject t : arr) {
        if (trainCount >= 4) break;
        strlcpy(trains[trainCount].name, t["name"] | "Train", sizeof(trains[trainCount].name));
        trains[trainCount].speed = t["speed"] | 0;
        strlcpy(trains[trainCount].direction, t["direction"] | "stopped", sizeof(trains[trainCount].direction));
        trains[trainCount].active = t["active"] | false;
        trains[trainCount].lastUpdate = millis();
        trainCount++;
      }
      Serial.printf("[HTTP] Fetched %d trains\n", trainCount);
    }
  }
  http.end();
}

void fetchWeatherHTTP() {
  if (!staConnected) return;

  String url = String("http://") + API_HOST + ":" + API_PORT + "/api/weather";
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      strlcpy(weather.condition, doc["condition"] | "unknown", sizeof(weather.condition));
      weather.tempC = doc["temp"] | 0;
      weather.humidity = doc["humidity"] | 0;
      strlcpy(weather.location, doc["location"] | "Unknown", sizeof(weather.location));
      Serial.printf("[HTTP] Weather: %d°C, %s\n", weather.tempC, weather.condition);
    }
  }
  http.end();
}

void fetchNewsHTTP() {
  if (!staConnected) return;

  String url = String("http://") + API_HOST + ":" + API_PORT + "/api/news";
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      JsonArray arr = doc["news"].as<JsonArray>();
      newsCount = 0;
      for (JsonObject n : arr) {
        if (newsCount >= 3) break;
        strlcpy(news[newsCount].headline, n["headline"] | "News", sizeof(news[newsCount].headline));
        strlcpy(news[newsCount].source, n["source"] | "Source", sizeof(news[newsCount].source));
        newsCount++;
      }
      Serial.printf("[HTTP] Fetched %d news items\n", newsCount);
    }
  }
  http.end();
}

void fetchScheduleHTTP() {
  if (!staConnected) return;

  String url = String("http://") + API_HOST + ":" + API_PORT + "/api/schedule";
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      JsonArray arr = doc["schedule"].as<JsonArray>();
      scheduleCount = 0;
      for (JsonObject s : arr) {
        if (scheduleCount >= 4) break;
        strlcpy(schedule[scheduleCount].time, s["time"] | "00:00", sizeof(schedule[scheduleCount].time));
        strlcpy(schedule[scheduleCount].train, s["train"] | "Train", sizeof(schedule[scheduleCount].train));
        strlcpy(schedule[scheduleCount].destination, s["dest"] | "Dest", sizeof(schedule[scheduleCount].destination));
        schedule[scheduleCount].platform = s["platform"] | 1;
        scheduleCount++;
      }
      Serial.printf("[HTTP] Fetched %d schedule entries\n", scheduleCount);
    }
  }
  http.end();
}

void fetchAllDataHTTP() {
  fetchTrainsHTTP();
  fetchScheduleHTTP();
  fetchNewsHTTP();
  fetchWeatherHTTP();
}

void setupWebSocketServer() {
  if (!staConnected) return;

  ws.onEvent(onWsEvent);
  asyncServer.addHandler(&ws);
  asyncServer.begin();
  Serial.printf("[WS] WebSocket server started on port 81 at ws://%s:81/ws\n", staIP.c_str());
}

// ============== SETUP FUNCTIONS ==============
void setupOLED() {
  Serial.println("Initializing OLED...");
  pinMode(OLED_SDA, OUTPUT);
  pinMode(OLED_SCL, OUTPUT);

  u8g2.begin();
  u8g2.setContrast(255);
  Serial.println("OLED initialized");

  // Boot animation - train moving across screen
  for (int i = -30; i <= 80; i += 3) {
    u8g2.clearBuffer();

    // Draw tracks
    u8g2.drawHLine(X, Y + 38, 72);
    u8g2.drawHLine(X, Y + 40, 72);

    // Draw train
    u8g2.drawXBMP(X + i, Y + 26, ICON_BOOT_TRAIN_W, ICON_BOOT_TRAIN_H, icon_boot_train);

    // Smoke puffs
    if (i > 0) {
      int smokeX = X + i - 4;
      if ((i / 3) % 2 == 0) {
        u8g2.drawCircle(smokeX, Y + 24, 2);
        u8g2.drawCircle(smokeX - 6, Y + 22, 3);
      } else {
        u8g2.drawCircle(smokeX - 2, Y + 23, 2);
        u8g2.drawCircle(smokeX - 8, Y + 21, 2);
      }
    }

    u8g2.sendBuffer();
    delay(30);
  }

  // Show title with fade-in effect
  for (int frame = 0; frame < 5; frame++) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    drawCentered(Y + 22, "RailCommand", 6);
    u8g2.setFont(u8g2_font_5x7_tr);
    drawCentered(Y + 33, "v1.0", 5);

    // Draw decorative lines that grow
    int lineLen = frame * 12;
    u8g2.drawHLine(X + 36 - lineLen/2, Y + 12, lineLen);
    u8g2.drawHLine(X + 36 - lineLen/2, Y + 38, lineLen);

    u8g2.sendBuffer();
    delay(80);
  }
  delay(600);
}

void setupWiFiAP() {
  // Use NODE_NAME for device identification
  deviceName = String(DEVICE_NAME_PREFIX) + NODE_NAME;
  apSSID = deviceName;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSSID.c_str(), apPassword.c_str());

  Serial.printf("AP Started: %s\n", apSSID.c_str());
  Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("Node ID: %d, Name: %s\n", NODE_ID, NODE_NAME);
}

void setupWiFiStation() {
  prefs.begin("wifi", true);
  staSSID = prefs.getString("ssid", "");
  staPassword = prefs.getString("pass", "");
  prefs.end();

  if (staSSID.length() > 0) {
    Serial.printf("Connecting to: %s\n", staSSID.c_str());

    // Configure static IP based on NODE_ID
    IPAddress staticIP(192, 168, 0, STATIC_IP_LAST_OCTET);
    IPAddress gateway(192, 168, 0, 1);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress dns(8, 8, 8, 8);

    if (!WiFi.config(staticIP, gateway, subnet, dns)) {
      Serial.println("Static IP config failed, using DHCP");
    } else {
      Serial.printf("Static IP configured: 192.168.0.%d\n", STATIC_IP_LAST_OCTET);
    }

    WiFi.begin(staSSID.c_str(), staPassword.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      u8g2.clearBuffer();

      u8g2.drawXBMP(X + 36 - ICON_WIFI_W/2, Y + 12, ICON_WIFI_W, ICON_WIFI_H, icon_wifi);

      u8g2.setFont(u8g2_font_6x10_tr);
      drawCentered(Y + 30, "Connecting", 6);

      u8g2.setFont(u8g2_font_5x7_tr);
      String dots = "";
      for (int d = 0; d < (attempts % 4); d++) dots += ".";
      drawCentered(Y + 38, dots.c_str(), 5);

      u8g2.drawFrame(X + 10, Y + 41, 52, 5);
      u8g2.drawBox(X + 11, Y + 42, (50 * attempts) / 20, 3);

      u8g2.sendBuffer();

      delay(500);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      staConnected = true;
      staIP = WiFi.localIP().toString();
      Serial.printf("\nConnected! IP: %s\n", staIP.c_str());

      u8g2.clearBuffer();
      u8g2.drawXBMP(X + 36 - ICON_WIFI_W/2, Y + 12, ICON_WIFI_W, ICON_WIFI_H, icon_wifi);
      u8g2.setFont(u8g2_font_6x10_tr);
      drawCentered(Y + 30, "Connected!", 6);
      u8g2.setFont(u8g2_font_5x7_tr);
      drawCentered(Y + 40, staIP.c_str(), 5);
      u8g2.sendBuffer();
      delay(1000);

      setupWebSocketServer();
      // Fetch initial data via HTTP
      fetchAllDataHTTP();
    } else {
      Serial.println("\nConnection failed");
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tr);
      drawCentered(Y + 30, "Failed", 6);
      u8g2.sendBuffer();
      delay(1000);
    }
  }
}

void setupBLE() {
  BLEDevice::init(deviceName.c_str());
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(BLE_SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    BLE_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );

  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
  pCharacteristic->setValue("Ready");

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  BLEDevice::startAdvertising();

  Serial.println("BLE Started");
}

void saveWiFiCredentials() {
  prefs.begin("wifi", false);
  prefs.putString("ssid", staSSID);
  prefs.putString("pass", staPassword);
  prefs.end();
  Serial.println("Credentials saved");
}

// ============== WEB SERVER ==============
String getWebPage() {
  unsigned long uptime = (millis() - bootTime) / 1000;
  char uptimeStr[12];
  snprintf(uptimeStr, sizeof(uptimeStr), "%02lu:%02lu:%02lu", uptime/3600, (uptime%3600)/60, uptime%60);

  int memPercent = 100 - (ESP.getFreeHeap() * 100 / 327680);

  String html = R"(<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="10">
<title>)" + deviceName + R"(</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,sans-serif;background:#0a0a0f;color:#e8e8f0;padding:20px}
.container{max-width:480px;margin:0 auto}
h1{font-size:1.4em;background:linear-gradient(135deg,#00d4ff,#a855f7);-webkit-background-clip:text;-webkit-text-fill-color:transparent;text-align:center;margin-bottom:8px}
.subtitle{text-align:center;color:#8888aa;font-size:0.85em;margin-bottom:20px}
.card{background:rgba(20,25,40,0.8);border:1px solid rgba(255,255,255,0.08);border-radius:12px;padding:16px;margin-bottom:12px}
.card h2{font-size:1em;margin-bottom:12px;display:flex;align-items:center;gap:8px}
.row{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid rgba(255,255,255,0.05)}
.row:last-child{border:none}
.label{color:#8888aa}
.value{font-family:monospace}
.badge{padding:2px 8px;border-radius:10px;font-size:0.8em}
.online{background:rgba(0,255,136,0.15);color:#00ff88}
.offline{background:rgba(255,68,102,0.15);color:#ff4466}
.waiting{background:rgba(255,170,0,0.15);color:#ffaa00}
input{width:100%;padding:12px;background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.1);border-radius:8px;color:#e8e8f0;margin-bottom:10px}
button{width:100%;padding:12px;border:none;border-radius:8px;font-weight:600;cursor:pointer}
.btn-primary{background:linear-gradient(135deg,#00d4ff,#0099bb);color:#000}
.btn-danger{background:rgba(255,68,102,0.2);color:#ff4466;border:1px solid rgba(255,68,102,0.3);margin-top:10px}
.train-item{background:rgba(0,212,255,0.1);border-radius:8px;padding:10px;margin-bottom:8px}
.train-name{font-weight:600;color:#00d4ff}
.train-status{display:flex;justify-content:space-between;margin-top:4px;font-size:0.85em;color:#8888aa}
</style></head><body>
<div class="container">
<h1>)" + deviceName + R"(</h1>
<div class="subtitle">RailCommand Node • )" + String(uptimeStr) + R"( uptime</div>

<div class="card">
<h2>📶 Network</h2>
<div class="row"><span class="label">AP</span><span class="value">)" + apSSID + R"(</span></div>
<div class="row"><span class="label">AP IP</span><span class="value">)" + WiFi.softAPIP().toString() + R"(</span></div>
<div class="row"><span class="label">Station</span><span class="badge )" + (staConnected ? "online\">Connected" : "offline\">Disconnected") + R"(</span></div>)" +
(staConnected ? "<div class=\"row\"><span class=\"label\">Station IP</span><span class=\"value\">" + staIP + "</span></div>" : "") + R"(
<div class="row"><span class="label">WebSocket</span><span class="badge )" + (wsConnected ? "online\">Connected" : "waiting\">Disconnected") + R"(</span></div>
</div>

<div class="card">
<h2>🚂 Active Trains</h2>)";

  if (trainCount == 0) {
    html += "<div style=\"color:#8888aa;text-align:center;padding:10px\">No active trains</div>";
  } else {
    for (int i = 0; i < trainCount; i++) {
      if (trains[i].active) {
        html += "<div class=\"train-item\"><div class=\"train-name\">" + String(trains[i].name) + "</div>";
        html += "<div class=\"train-status\"><span>" + String(trains[i].direction) + "</span>";
        html += "<span>" + String(trains[i].speed) + "%</span></div></div>";
      }
    }
  }

  html += R"(</div>

<div class="card">
<h2>⚙️ WiFi Setup</h2>
<form action="/save" method="POST">
<input type="text" name="ssid" placeholder="WiFi Network" value=")" + staSSID + R"(">
<input type="password" name="pass" placeholder="Password">
<button type="submit" class="btn-primary">Save & Connect</button>
</form>
<button onclick="location.href='/reboot'" class="btn-danger">Reboot</button>
</div>

<div class="card">
<h2>📊 System</h2>
<div class="row"><span class="label">Memory</span><span class="value">)" + String(memPercent) + R"(%</span></div>
<div class="row"><span class="label">Free Heap</span><span class="value">)" + String(ESP.getFreeHeap()/1024) + R"( KB</span></div>
<div class="row"><span class="label">BLE Clients</span><span class="value">)" + String(bleClientCount) + R"(</span></div>
</div>
</div></body></html>)";

  return html;
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", getWebPage());
  });

  server.on("/save", HTTP_POST, []() {
    staSSID = server.arg("ssid");
    staPassword = server.arg("pass");
    saveWiFiCredentials();
    server.send(200, "text/html", "<html><body style='background:#0a0a0f;color:#fff;display:flex;justify-content:center;align-items:center;height:100vh'><h2>Saved! Rebooting...</h2></body></html>");
    delay(2000);
    ESP.restart();
  });

  server.on("/api/status", HTTP_GET, []() {
    StaticJsonDocument<512> doc;
    doc["device"] = deviceName;
    doc["uptime"] = (millis() - bootTime) / 1000;
    doc["heap_free"] = ESP.getFreeHeap();
    doc["wifi_connected"] = staConnected;
    doc["wifi_ip"] = staIP;
    doc["ws_connected"] = wsConnected;
    doc["ble_clients"] = bleClientCount;
    doc["train_count"] = trainCount;
    doc["screen"] = currentScreen;
    doc["led_count"] = HOUSE_LED_COUNT;
    JsonArray leds = doc.createNestedArray("leds");
    for (int i = 0; i < HOUSE_LED_COUNT; i++) leds.add(houseLedState[i]);

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.on("/api/trains", HTTP_GET, []() {
    StaticJsonDocument<512> doc;
    JsonArray arr = doc.createNestedArray("trains");
    for (int i = 0; i < trainCount; i++) {
      JsonObject t = arr.createNestedObject();
      t["name"] = trains[i].name;
      t["speed"] = trains[i].speed;
      t["direction"] = trains[i].direction;
      t["active"] = trains[i].active;
    }
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  // GET /api/led → report all 6 LED states
  server.on("/api/led", HTTP_GET, []() {
    StaticJsonDocument<128> d;
    JsonArray a = d.createNestedArray("leds");
    for (int i = 0; i < HOUSE_LED_COUNT; i++) a.add(houseLedState[i]);
    String j; serializeJson(d, j);
    server.send(200, "application/json", j);
  });

  // POST /api/led/<n>/<on|off> → drive one building LED.
  // The ESP32 WebServer doesn't do path parameters out of the box, so we
  // use server.onNotFound() as a one-line router for this URL family.
  server.onNotFound([]() {
    String uri = server.uri();
    // Everything else: honest 404.
    if (!uri.startsWith("/api/led/")) {
      server.send(404, "application/json", "{\"error\":\"not found\"}");
      return;
    }
    // Only POST mutates state — GETs on a non-registered /api/led/ path
    // get a method-not-allowed, never a toggle.
    if (server.method() != HTTP_POST) {
      server.send(405, "application/json", "{\"error\":\"method not allowed — POST only\"}");
      return;
    }
    // Expect "/api/led/<n>/<state>"
    int firstSlash = uri.indexOf('/', 9);
    if (firstSlash < 0) {
      server.send(400, "application/json", "{\"error\":\"expected /api/led/<n>/<state>\"}");
      return;
    }
    String idxStr = uri.substring(9, firstSlash);
    if (idxStr.length() == 0) {
      server.send(400, "application/json", "{\"error\":\"missing led index\"}");
      return;
    }
    // toInt() silently returns 0 on non-numeric input; reject explicitly.
    for (size_t i = 0; i < idxStr.length(); i++) {
      if (!isDigit(idxStr[i])) {
        server.send(400, "application/json", "{\"error\":\"led index must be numeric\"}");
        return;
      }
    }
    int n = idxStr.toInt();
    if (n < 0 || n >= HOUSE_LED_COUNT) {
      server.send(400, "application/json", "{\"error\":\"led index out of range\"}");
      return;
    }
    String state = uri.substring(firstSlash + 1);
    bool on;
    if      (state == "on"  || state == "1" || state == "true")  on = true;
    else if (state == "off" || state == "0" || state == "false") on = false;
    else { server.send(400, "application/json", "{\"error\":\"state must be on|off\"}"); return; }
    digitalWrite(HOUSE_PINS[n], on ? HIGH : LOW);
    houseLedState[n] = on;
    StaticJsonDocument<96> d;
    d["led"] = n;
    d["on"]  = on;
    String j; serializeJson(d, j);
    server.send(200, "application/json", j);
  });

  server.on("/reboot", HTTP_GET, []() {
    server.send(200, "text/plain", "Rebooting...");
    delay(500);
    ESP.restart();
  });

  server.begin();
  Serial.println("Web server started");
}

// ============== ANIMATION HELPERS ==============
void drawAnimatedTrain(int x, int y, bool forward, int speed) {
  // Draw train icon based on direction
  if (forward) {
    u8g2.drawXBMP(x, y, ICON_LOCO_W, ICON_LOCO_H, icon_loco_r);
  } else {
    u8g2.drawXBMP(x, y, ICON_LOCO_W, ICON_LOCO_H, icon_loco_l);
  }

  // Animated smoke puffs when moving
  if (speed > 0) {
    int smokeOffset = (animFrame / 2) % 8;
    int smokeX = forward ? x - 2 - smokeOffset : x + ICON_LOCO_W + 2 + smokeOffset;
    if ((animFrame / 4) % 2 == 0) {
      u8g2.drawXBMP(smokeX, y - 3, ICON_SMOKE_W, ICON_SMOKE_H, icon_smoke);
    }
  }
}

void drawPulsingDot(int x, int y, bool active) {
  if (active) {
    // Pulsing animation for active connection
    int radius = 2 + ((animFrame / 5) % 2);
    u8g2.drawDisc(x, y, radius);
  } else {
    // Blinking for inactive
    if ((animFrame / 8) % 2 == 0) {
      u8g2.drawCircle(x, y, 2);
    }
  }
}

void drawSpeedBar(int x, int y, int width, int speed) {
  // Background
  u8g2.drawFrame(x, y, width, 4);

  // Fill based on speed
  int fillWidth = (width - 2) * speed / 100;
  if (fillWidth > 0) {
    // Animated fill pattern for active trains
    for (int i = 0; i < fillWidth; i++) {
      if ((i + animFrame / 2) % 3 != 0 || speed < 20) {
        u8g2.drawVLine(x + 1 + i, y + 1, 2);
      }
    }
  }
}

void drawScrollingText(int x, int y, const char* text, int maxWidth) {
  int textLen = strlen(text);
  int textWidth = textLen * 5;  // Width with 5x8 font

  if (textWidth <= maxWidth) {
    u8g2.drawStr(x, y, text);
  } else {
    // Scroll animation
    int scrollOffset = (animFrame / 3) % (textWidth + 20);
    u8g2.setClipWindow(x, 0, x + maxWidth, 64);
    u8g2.drawStr(x - scrollOffset + maxWidth, y, text);
    u8g2.setMaxClipWindow();
  }
}

// ============== DISPLAY SCREENS ==============
// Helper to center text
void drawCentered(int y, const char* text, int charWidth) {
  int textWidth = strlen(text) * charWidth;
  int xPos = X + (72 - textWidth) / 2;
  u8g2.drawStr(xPos, y, text);
}

void drawWiFiScreen() {
  // Layout: icon Y+12, title Y+30, content Y+38/Y+46
  u8g2.drawXBMP(X + 36 - ICON_WIFI_W/2, Y + 12, ICON_WIFI_W, ICON_WIFI_H, icon_wifi);

  u8g2.setFont(u8g2_font_6x10_tr);
  drawCentered(Y + 30, "WiFi", 6);

  if (staConnected) {
    u8g2.setFont(u8g2_font_5x7_tr);
    drawCentered(Y + 38, staIP.c_str(), 5);
    drawCentered(Y + 46, wsConnected ? "Online" : "Offline", 5);
  } else {
    u8g2.setFont(u8g2_font_5x7_tr);
    if (staSSID.length() > 0) {
      String dots = "";
      for (int i = 0; i < (animFrame / 8) % 4; i++) dots += ".";
      drawCentered(Y + 40, ("Connect" + dots).c_str(), 5);
    } else {
      drawCentered(Y + 40, "Not setup", 5);
    }
  }
}

void drawBLEScreen() {
  u8g2.drawXBMP(X + 36 - ICON_BLE_W/2, Y + 12, ICON_BLE_W, ICON_BLE_H, icon_ble);

  u8g2.setFont(u8g2_font_6x10_tr);
  drawCentered(Y + 30, "Bluetooth", 6);

  u8g2.setFont(u8g2_font_5x7_tr);
  drawCentered(Y + 38, deviceName.substring(7).c_str(), 5);

  if (bleClientConnected) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d paired", bleClientCount);
    drawCentered(Y + 46, buf, 5);
  } else {
    drawCentered(Y + 46, "Discoverable", 5);
  }
}

void drawTrainScreen() {
  u8g2.drawXBMP(X + 36 - ICON_TRAIN_W/2, Y + 12, ICON_TRAIN_W, ICON_TRAIN_H, icon_train);

  u8g2.setFont(u8g2_font_6x10_tr);
  char header[16];
  snprintf(header, sizeof(header), "Trains (%d)", trainCount);
  drawCentered(Y + 30, header, 6);

  u8g2.setFont(u8g2_font_5x7_tr);

  if (trainCount == 0) {
    drawCentered(Y + 40, "No active", 5);
  } else {
    bool fwd = strcmp(trains[0].direction, "forward") == 0;
    char line[20];
    snprintf(line, sizeof(line), "%s %s %d%%", fwd ? ">>" : "<<", trains[0].name, trains[0].speed);
    if (strlen(line) > 14) line[14] = '\0';
    drawCentered(Y + 38, line, 5);

    if (trainCount > 1) {
      fwd = strcmp(trains[1].direction, "forward") == 0;
      snprintf(line, sizeof(line), "%s %s %d%%", fwd ? ">>" : "<<", trains[1].name, trains[1].speed);
      if (strlen(line) > 14) line[14] = '\0';
      drawCentered(Y + 46, line, 5);
    }
  }
}

void drawScheduleScreen() {
  u8g2.drawXBMP(X + 36 - ICON_CLOCK_W/2, Y + 12, ICON_CLOCK_W, ICON_CLOCK_H, icon_clock);

  u8g2.setFont(u8g2_font_6x10_tr);
  drawCentered(Y + 30, "Schedule", 6);

  u8g2.setFont(u8g2_font_5x7_tr);

  if (scheduleCount == 0) {
    drawCentered(Y + 40, "No departures", 5);
  } else {
    char line[20];
    snprintf(line, sizeof(line), "%s %s", schedule[0].time, schedule[0].destination);
    if (strlen(line) > 14) line[14] = '\0';
    drawCentered(Y + 38, line, 5);

    snprintf(line, sizeof(line), "Platform %d", schedule[0].platform);
    drawCentered(Y + 46, line, 5);
  }
}

void drawStatsScreen() {
  u8g2.drawXBMP(X + 36 - ICON_STATS_W/2, Y + 12, ICON_STATS_W, ICON_STATS_H, icon_stats);

  u8g2.setFont(u8g2_font_6x10_tr);
  drawCentered(Y + 30, "System", 6);

  u8g2.setFont(u8g2_font_5x7_tr);

  int memPercent = 100 - (ESP.getFreeHeap() * 100 / 327680);
  char buf[20];
  snprintf(buf, sizeof(buf), "Memory: %d%%", memPercent);
  drawCentered(Y + 38, buf, 5);

  unsigned long up = (millis() - bootTime) / 1000;
  snprintf(buf, sizeof(buf), "Up %02lu:%02lu:%02lu", up/3600, (up%3600)/60, up%60);
  drawCentered(Y + 46, buf, 5);
}

void drawNewsScreen() {
  u8g2.drawXBMP(X + 36 - ICON_NEWS_W/2, Y + 12, ICON_NEWS_W, ICON_NEWS_H, icon_news);

  u8g2.setFont(u8g2_font_6x10_tr);
  drawCentered(Y + 30, "News", 6);

  if (newsCount == 0) {
    u8g2.setFont(u8g2_font_5x7_tr);
    drawCentered(Y + 40, "No headlines", 5);
  } else {
    u8g2.setFont(u8g2_font_5x7_tr);

    int newsIdx = (millis() / 4000) % newsCount;
    int headlineLen = strlen(news[newsIdx].headline);

    if (headlineLen <= 14) {
      drawCentered(Y + 38, news[newsIdx].headline, 5);
    } else {
      int totalScroll = (headlineLen - 13) * 5;
      int scrollPx = (animFrame * 2) % (totalScroll + 60);
      int offset = scrollPx > 30 ? scrollPx - 30 : 0;
      if (offset > totalScroll) offset = totalScroll;

      u8g2.setClipWindow(X + 2, Y + 31, X + 70, Y + 42);
      u8g2.drawStr(X + 2 - offset, Y + 38, news[newsIdx].headline);
      u8g2.setMaxClipWindow();
    }

    drawCentered(Y + 46, news[newsIdx].source, 5);
  }
}

void drawWeatherScreen() {
  bool isSunny = (strcmp(weather.condition, "sunny") == 0 ||
                  strcmp(weather.condition, "clear") == 0 ||
                  strstr(weather.condition, "sun") != NULL);

  if (isSunny) {
    u8g2.drawXBMP(X + 36 - ICON_SUN_W/2, Y + 12, ICON_SUN_W, ICON_SUN_H, icon_sun);
  } else {
    u8g2.drawXBMP(X + 36 - ICON_CLOUD_W/2, Y + 12, ICON_CLOUD_W, ICON_CLOUD_H, icon_cloud);
  }

  u8g2.setFont(u8g2_font_6x10_tr);
  drawCentered(Y + 30, "Weather", 6);

  u8g2.setFont(u8g2_font_5x7_tr);

  if (weather.tempC == 0 && weather.humidity == 0 && strcmp(weather.condition, "unknown") == 0) {
    drawCentered(Y + 40, "Loading...", 5);
  } else {
    char buf[20];
    snprintf(buf, sizeof(buf), "%dC  %d%%", weather.tempC, weather.humidity);
    drawCentered(Y + 38, buf, 5);
    drawCentered(Y + 46, weather.condition, 5);
  }
}

void drawTransition(int nextScreen) {
  const char* names[] = {"WiFi", "BLE", "Trains", "Schedule", "Stats", "News", "Weather"};
  int centerX = X + 36;

  // Animated transition - slide in from right
  for (int frame = 0; frame < 6; frame++) {
    u8g2.clearBuffer();

    int offsetX = (5 - frame) * 15;
    int iconY = Y + 12;

    int ix = centerX + offsetX;
    switch (nextScreen) {
      case 0: u8g2.drawXBMP(ix - ICON_WIFI_W/2, iconY, ICON_WIFI_W, ICON_WIFI_H, icon_wifi); break;
      case 1: u8g2.drawXBMP(ix - ICON_BLE_W/2, iconY, ICON_BLE_W, ICON_BLE_H, icon_ble); break;
      case 2: u8g2.drawXBMP(ix - ICON_TRAIN_W/2, iconY, ICON_TRAIN_W, ICON_TRAIN_H, icon_train); break;
      case 3: u8g2.drawXBMP(ix - ICON_CLOCK_W/2, iconY, ICON_CLOCK_W, ICON_CLOCK_H, icon_clock); break;
      case 4: u8g2.drawXBMP(ix - ICON_STATS_W/2, iconY, ICON_STATS_W, ICON_STATS_H, icon_stats); break;
      case 5: u8g2.drawXBMP(ix - ICON_NEWS_W/2, iconY, ICON_NEWS_W, ICON_NEWS_H, icon_news); break;
      case 6: u8g2.drawXBMP(ix - ICON_SUN_W/2, iconY, ICON_SUN_W, ICON_SUN_H, icon_sun); break;
    }

    u8g2.setFont(u8g2_font_6x10_tr);
    int nameLen = strlen(names[nextScreen]) * 6;
    u8g2.drawStr(X + (72 - nameLen) / 2 + offsetX, Y + 30, names[nextScreen]);

    u8g2.sendBuffer();
    delay(40);
  }

  delay(200);
}

void updateDisplay() {
  u8g2.clearBuffer();
  animFrame = (animFrame + 1) % 60;

  switch (currentScreen) {
    case 0: drawWiFiScreen(); break;
    case 1: drawBLEScreen(); break;
    case 2: drawTrainScreen(); break;
    case 3: drawScheduleScreen(); break;
    case 4: drawStatsScreen(); break;
    case 5: drawNewsScreen(); break;
    case 6: drawWeatherScreen(); break;
  }

  u8g2.sendBuffer();
}

// ============== MAIN ==============
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== RailCommand ESP32-C3 ===");

  bootTime = millis();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  pinMode(BOOT_BTN, INPUT_PULLUP);

  // House LED drivers — 6 outputs per node, driven HIGH to light the LED
  // via a 2N3904 low-side switch. All start off.
  for (int i = 0; i < HOUSE_LED_COUNT; i++) {
    pinMode(HOUSE_PINS[i], OUTPUT);
    digitalWrite(HOUSE_PINS[i], LOW);
    houseLedState[i] = false;
  }

  setupOLED();
  setupWiFiAP();
  setupWiFiStation();
  setupBLE();
  setupWebServer();

  digitalWrite(LED_PIN, LOW);
  Serial.println("=== Ready ===");
}

void loop() {
  // Clean up old WebSocket clients
  ws.cleanupClients();

  // Handle web server
  server.handleClient();

  // Periodic API update
  if (staConnected && millis() - lastApiUpdate > API_UPDATE_INTERVAL) {
    lastApiUpdate = millis();
    sendStatusToAPI();
  }

  // HTTP polling fallback when WebSocket not connected
  if (staConnected && !wsConnected && millis() - lastHttpPoll > HTTP_POLL_INTERVAL) {
    lastHttpPoll = millis();
    fetchAllDataHTTP();
  }

  // Button handling - cycle screens manually (resets auto-cycle timer)
  if (digitalRead(BOOT_BTN) == LOW && millis() - lastButtonPress > 300) {
    lastButtonPress = millis();
    lastScreenCycle = millis();  // Reset auto-cycle timer
    int nextScreen = (currentScreen + 1) % TOTAL_SCREENS;
    drawTransition(nextScreen);
    currentScreen = nextScreen;
    Serial.printf("Screen: %d\n", currentScreen);
  }

  // Auto-cycle screens - longer pause on news (5) and weather (6)
  unsigned long cycleInterval = (currentScreen == 5 || currentScreen == 6)
    ? NEWS_WEATHER_INTERVAL : SCREEN_CYCLE_INTERVAL;

  if (millis() - lastScreenCycle > cycleInterval) {
    lastScreenCycle = millis();
    int nextScreen = (currentScreen + 1) % TOTAL_SCREENS;
    drawTransition(nextScreen);
    currentScreen = nextScreen;
  }

  // Update display
  updateDisplay();

  delay(50);
}
