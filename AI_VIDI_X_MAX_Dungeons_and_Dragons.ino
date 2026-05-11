/* 
 * VIDI X — D&D LLM Adventure (EN) — ESP32 + ILI9341 + ESP32_AI_Connect
 * Author: VIDI X Team
 * 
 * Version 0.3 - 06.05.2026.
 * - updated for ESP32 3.x support
 * - supports accented characters
 *
 * Version 0.x - xx.xx.xxxx.
 * (Add a short description of important code changes for future versions here) 
 *
 * Dependencies:
 *  - ESP32 core for Arduino
 *  - Adafruit_GFX, Adafruit_ILI9341
 *  - ArduinoJson (7.x or 6.x)
 *  - ESP32_AI_Connect (https://github.com/AvantMaker/ESP32_AI_Connect)
 *  - my_info.h (your Wi-Fi and LLM API details)
 *
 * Buttons:
 *  - BTN_A (GPIO32): confirm the selected option
 *  - BTN_B (GPIO33): redraw the screen / short help
 *  - BTN_START (GPIO39): new game (context reset)
 *  - BTN_UD (GPIO35 analog): UP/DOWN navigation (UP ~ >4000, DOWN ~ 1800..2200)
 *  - BTN_LR (GPIO34 analog): not required, but kept for future expansion
 *
 * Screen:
 *  - Landscape (setRotation(3)), 320x240
 *  - Top area: title + scene (wrapped)
 *  - Middle area: list of options "1) ... 2) ..." (wrapped)
 *  - Bottom: 4 soft buttons 1–4 with highlight
 *
 * LLM Protocol:
 *  - System prompt (EN) requests strict JSON without code blocks:
 *    { "scene": "...", "options": ["..."], "end": false, "hint": "..." }
 *  - Up to 4 short options. Scene <= 280 characters.
 *  - If "end": true, the game ends with a short epilogue in "scene".
 *
 * Note:
 *  - The LLM output must be JSON WITHOUT ```; the parser still sanitizes the response and extracts the first {...} block.
 *  - The game context is sent to the model inside the user prompt (a short summary of previous moves).
 * 
 * ---------------------------
 * Tested with these versions:
 * ---------------------------
 * ESP32 v3.3.8
 * ESP32 Wrover Kit (All versions)
 * Arduino IDE 2.3.6
 * Partition Scheme: Huge App (3MB No OTA/1MB SPIFFS)
 * 
 * #include <LovyanGFX.hpp> - version 1.2.19
 * #include <WiFi.h> - default version 
 * #include <ArduinoJson.h> - version 7.4.2
 * #include <ESP32_AI_Connect.h> - version 0.5.16
 * ---------------------------
 */

#define USE_AI_API_DEEPSEEK
#define ENABLE_DEBUG_OUTPUT

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <WiFi.h>
#include <ArduinoJson.h>
#include <ESP32_AI_Connect.h>
#include <WiFiClientSecure.h>
#include <esp_arduino_version.h>
#include "my_info.h"

// ---------- LGFX for VIDI X (ILI9341 / VSPI) ----------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI       _bus;

public:
  LGFX() {
    { // SPI bus
      auto c = _bus.config();
      c.spi_host    = VSPI_HOST;
      c.spi_mode    = 0;
      c.freq_write  = 40000000;
      c.freq_read   = 16000000;
      c.spi_3wire   = true;
      c.use_lock    = true;
      c.dma_channel = 1;
      c.pin_sclk    = 18;
      c.pin_mosi    = 23;
      c.pin_miso    = 19;
      c.pin_dc      = 21;
      _bus.config(c);
      _panel.setBus(&_bus);
    }

    { // ILI9341 panel
      auto c = _panel.config();
      c.pin_cs          = 5;
      c.pin_rst         = -1;
      c.pin_busy        = -1;
      c.memory_width    = 240;
      c.memory_height   = 320;
      c.panel_width     = 240;
      c.panel_height    = 320;
      c.offset_x        = 0;
      c.offset_y        = 0;
      c.offset_rotation = 1;    // VIDI X landscape correction
      c.dummy_read_pixel = 8;
      c.dummy_read_bits  = 1;
      c.readable        = true;
      c.invert          = false;
      c.rgb_order       = false;
      c.dlen_16bit      = false;
      c.bus_shared      = true;
      _panel.config(c);
    }

    setPanel(&_panel);
  }
};

static LGFX lcd;

// ---------- NETWORK SELF TEST ----------

void networkSelfTest(const char* host = "openrouter.ai", uint16_t port = 443) {
  Serial.println();
  Serial.println("========== NETWORK SELF-TEST ==========");

  wl_status_t st = WiFi.status();
  Serial.print("WiFi.status(): ");
  Serial.println((int)st);

  if (st != WL_CONNECTED) {
    Serial.println("NOT CONNECTED -> stopping self-test.");
    Serial.println("=======================================");
    return;
  }

  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("GW: ");
  Serial.println(WiFi.gatewayIP());

  Serial.print("Mask: ");
  Serial.println(WiFi.subnetMask());

  Serial.print("DNS0: ");
  Serial.println(WiFi.dnsIP(0));

  Serial.print("DNS1: ");
  Serial.println(WiFi.dnsIP(1));

  Serial.print("RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  IPAddress ip;
  Serial.print("DNS lookup ");
  Serial.print(host);
  Serial.print(" ... ");

  bool dnsOk = WiFi.hostByName(host, ip);
  Serial.println(dnsOk ? "OK" : "FAIL");

  if (dnsOk) {
    Serial.print("Resolved IP: ");
    Serial.println(ip);
  } else {
    Serial.println("DNS FAIL -> the problem is DNS or the network, or the host is blocked.");
    Serial.println("=======================================");
    return;
  }

  WiFiClientSecure client;
  client.setTimeout(8000);
  client.setInsecure(); // diagnostics only

  Serial.print("TCP/TLS connect ");
  Serial.print(host);
  Serial.print(":");
  Serial.print(port);
  Serial.print(" ... ");

  bool connOk = client.connect(host, port);
  Serial.println(connOk ? "OK" : "FAIL");

  if (!connOk) {
    Serial.println("connect() failed.");
    Serial.println("If this is FAIL -> the router/firewall/ISP is blocking access, or the host is not reachable from this network.");
    Serial.println("=======================================");
    return;
  }

  Serial.println("TLS + HTTP probe: GET / ...");

  client.print(String("GET / HTTP/1.1\r\n") +
               "Host: " + host + "\r\n" +
               "User-Agent: esp32-selftest\r\n" +
               "Connection: close\r\n\r\n");

  unsigned long t0 = millis();

  while (!client.available() && (millis() - t0) < 8000) {
    delay(10);
  }

  if (!client.available()) {
    Serial.println("No data received within timeout -> possible TLS issue, SNI issue, or the server is not responding.");
    client.stop();
    Serial.println("=======================================");
    return;
  }

  Serial.println("----- RESPONSE (first lines) -----");

  int lines = 0;
  while (client.available() && lines < 25) {
    String line = client.readStringUntil('\n');
    line.trim();
    Serial.println(line);
    lines++;
  }

  Serial.println("----- END RESPONSE -----");

  client.stop();

  Serial.println("SELF-TEST OK: DNS + TCP/TLS + basic HTTP are working.");
  Serial.println("=======================================");
  Serial.println();
}

void networkSelfTestSuite() {
  networkSelfTest("openrouter.ai", 443);
  networkSelfTest("example.com", 443);
}

// ---------- VIDI X PINS ----------
#define BTN_START   39
#define BTN_A       32
#define BTN_B       33
#define BTN_UD      35
#define BTN_LR      34
#define SPEAKER_PIN 25

// ---------- LEDC / SOUND ----------
#define SPEAKER_LEDC_CHANNEL 0
#define SPEAKER_LEDC_FREQ    2000
#define SPEAKER_LEDC_RES     8

// ---------- COLORS ----------
#define C_BLACK   0x0000
#define C_BLUE    0x001F
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_CYAN    0x07FF
#define C_MAGENTA 0xF81F
#define C_YELLOW  0xFFE0
#define C_WHITE   0xFFFF
#define C_GRAY    0x8410

// ---------- AI CLIENT ----------
ESP32_AI_Connect aiClient(platform, apiKey, model, customEndpoint);

// ---------- UI TOGGLE ----------
#define SHOW_SOFT_BTNS 0

// ---------- UI GEOMETRY ----------
const int W = 320;
const int H = 240;
const int PAD = 4;
const int HEADER_H = 26;

#if SHOW_SOFT_BTNS
const int BUTTON_BAR_H = 44;
#else
const int BUTTON_BAR_H = 0;
#endif

const int LINE_VSPACE = 0;
const int OPTION_VPAD = 1;
const int OPTION_VGAP = 2;

// ---------- STATE ----------
enum GameState {
  ST_BOOT,
  ST_IDLE,
  ST_WAIT_AI,
  ST_SHOW,
  ST_GAMEOVER
};

GameState gs = ST_BOOT;

String sceneText;
String options[4];

int optionCount = 0;
int selectedIdx = 0;

unsigned long lastBtnMs = 0;
const int debounceMs = 160;

String contextLog;
const size_t CONTEXT_MAX = 2000;

int gOptionsTop = HEADER_H + 100;
int optionYPos[4]   = {0, 0, 0, 0};
int optionHeight[4] = {20, 20, 20, 20};

// ---------- PROMPT ----------
const char* SYSTEM_PROMPT =
  "You are the Dungeon Master for an interactive D&D text adventure in English. "
  "Use clear, child-friendly English without slang. "
  "Respond strictly in JSON format, with no additional text and no code blocks. "
  "The JSON keys are: "
  "scene, options, end, and hint. "
  "scene is a string of up to 280 characters, 1 to 4 short sentences, with clear and immersive action. "
  "options is an array of up to 4 short imperative options, each up to 40 characters. "
  "end is a boolean. "
  "hint is a short tip of up to 60 characters. "
  "Keep the tone of D&D narration, but make it simple, playful, and understandable for children. "
  "Always include options and end. "
  "If it is the ending, end must be true and scene must be a short epilogue. "
  "Never output anything outside JSON. Do not write reasoning. JSON only.";

const char* NEW_GAME_SEED =
  "Start a new game. Genre: fantasy with magic and secrets. "
  "Location: an abandoned tower at the edge of a misty forest. "
  "Goal: find the secret grimoire before an ancient spirit awakens. "
  "Return JSON according to the rules. Do not write reasoning. JSON only.";

// ---------- DEBUG / ERROR HELPERS ----------
String lastAiFailWhere;
String lastAiFailWhy;

void aiFail(const char* where, const String& why) {
  lastAiFailWhere = where;
  lastAiFailWhy = why;

  Serial.println();
  Serial.println("===== AI FAIL =====");

  Serial.print("Where: ");
  Serial.println(where);

  Serial.print("Why:   ");
  Serial.println(why);

  Serial.print("LastError: ");
  Serial.println(aiClient.getLastError());

  Serial.println("===================");
  Serial.println();
}

// ---------- UTILITIES ----------
void beep(uint16_t f = 1800, uint16_t ms = 50) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(SPEAKER_PIN, f);
  delay(ms);
  ledcWriteTone(SPEAKER_PIN, 0);
#else
  ledcWriteTone(SPEAKER_LEDC_CHANNEL, f);
  delay(ms);
  ledcWriteTone(SPEAKER_LEDC_CHANNEL, 0);
#endif
}

bool edge(unsigned long now) {
  if (now - lastBtnMs > debounceMs) {
    lastBtnMs = now;
    return true;
  }

  return false;
}

int readUD() {
  return analogRead(BTN_UD);
}

bool isUp(int v) {
  return v > 4000;
}

bool isDown(int v) {
  return v > 1800 && v < 2200;
}

void trimContext() {
  if (contextLog.length() > CONTEXT_MAX) {
    contextLog.remove(0, contextLog.length() - CONTEXT_MAX);
  }
}

int lineH() {
  int lh = lcd.fontHeight();
  return lh > 0 ? lh : 14;
}

int printWrappedRetY(int x, int y, int w, const String& s, uint16_t fg, uint16_t bg) {
  lcd.setTextColor(fg, bg);

  int lh = lineH();
  int start = 0;
  int len = s.length();

  while (start < len) {
    int lastFit = start;
    int pos = start;

    while (pos <= len) {
      int nextSpace = s.indexOf(' ', pos);
      if (nextSpace == -1) nextSpace = len;

      String candidate = s.substring(start, nextSpace);

      if (lcd.textWidth(candidate) <= w) {
        lastFit = nextSpace;
        pos = nextSpace + 1;
      } else {
        break;
      }

      if (nextSpace == len) {
        break;
      }
    }

    if (lastFit == start) {
      int cut = start;

      while (cut < len && lcd.textWidth(s.substring(start, cut + 1)) <= w) {
        cut++;
      }

      if (cut == start) {
        cut++;
      }

      String line = s.substring(start, cut);
      lcd.setCursor(x, y);
      lcd.print(line);

      start = cut;

      while (start < len && s[start] == ' ') {
        start++;
      }
    } else {
      String line = s.substring(start, lastFit);
      lcd.setCursor(x, y);
      lcd.print(line);

      start = lastFit + 1;

      while (start < len && s[start] == ' ') {
        start++;
      }
    }

    y += lh + 2;
  }

  return y;
}

void splitToTwoLines(const String& full, int maxW, String& l1, String& l2) {
  if (lcd.textWidth(full) <= maxW) {
    l1 = full;
    l2 = "";
    return;
  }

  int lastFit = -1;
  int pos = 0;

  while (true) {
    int sp = full.indexOf(' ', pos);
    if (sp == -1) sp = full.length();

    String candidate = full.substring(0, sp);

    if (lcd.textWidth(candidate) <= maxW) {
      lastFit = sp;

      if (sp == full.length()) {
        break;
      }

      pos = sp + 1;
    } else {
      break;
    }
  }

  if (lastFit <= 0) {
    int cut = 1;

    while (cut < (int)full.length() && lcd.textWidth(full.substring(0, cut + 1)) <= maxW) {
      cut++;
    }

    l1 = full.substring(0, cut);
    l2 = full.substring(cut);
  } else {
    l1 = full.substring(0, lastFit);

    int start = lastFit;
    while (start < (int)full.length() && full[start] == ' ') {
      start++;
    }

    l2 = full.substring(start);
  }
}

void layoutOptions() {
  lcd.setFont(&fonts::efontJA_12);

  int w = W - PAD * 2;
  int y = gOptionsTop;

  for (int i = 0; i < optionCount; i++) {
    String full = String(i + 1) + ") " + options[i];

    String l1;
    String l2;
    splitToTwoLines(full, w, l1, l2);

    int rows = l2.length() ? 2 : 1;
    int h = 2 * OPTION_VPAD + rows * (lineH() + LINE_VSPACE);

    optionYPos[i] = y;
    optionHeight[i] = h;

    y += h + OPTION_VGAP;
  }

  int totalH = y - gOptionsTop;
  int bottom = H - BUTTON_BAR_H - 6;
  int minTop = HEADER_H + 42;

  if (gOptionsTop + totalH > bottom) {
    gOptionsTop = bottom - totalH;

    if (gOptionsTop < minTop) {
      gOptionsTop = minTop;
    }

    y = gOptionsTop;

    for (int i = 0; i < optionCount; i++) {
      optionYPos[i] = y;
      y += optionHeight[i] + OPTION_VGAP;
    }
  }
}

inline int optionY(int idx) {
  return optionYPos[idx];
}

inline int optionH(int idx) {
  return optionHeight[idx];
}

void drawOptionLine(int idx, bool selected) {
  lcd.setFont(&fonts::efontJA_12);

  int oy = optionY(idx);
  int h = optionH(idx);

  uint16_t bg = selected ? C_YELLOW : C_BLACK;
  uint16_t fg = selected ? C_RED : C_CYAN;

  lcd.fillRect(PAD - 2, oy - 2, W - PAD * 2 + 4, h + 3, bg);

  String full = String(idx + 1) + ") " + options[idx];

  String l1;
  String l2;
  splitToTwoLines(full, W - PAD * 2, l1, l2);

  lcd.setTextColor(fg, bg);

  int y = oy + OPTION_VPAD;

  lcd.setCursor(PAD, y);
  lcd.print(l1);

  if (l2.length()) {
    y += lineH() + LINE_VSPACE;
    lcd.setCursor(PAD, y);
    lcd.print(l2);
  }
}

void updateSelectionHighlight(int oldIdx, int newIdx) {
  if (oldIdx == newIdx) {
    return;
  }

  if (oldIdx >= 0 && oldIdx < optionCount) {
    drawOptionLine(oldIdx, false);
  }

  if (newIdx >= 0 && newIdx < optionCount) {
    drawOptionLine(newIdx, true);
  }
}

String extractJsonObject(const String& raw) {
  int a = raw.indexOf('{');
  int b = raw.lastIndexOf('}');

  if (a >= 0 && b > a) {
    return raw.substring(a, b + 1);
  }

  return String();
}

// ---------- DRAWING ----------
void drawHeader(const char* title = "VIDI X D&D AVANTURA") {
  lcd.fillRect(0, 0, W, HEADER_H, C_RED);

  lcd.setFont(&fonts::efontJA_16);
  lcd.setTextColor(C_YELLOW, C_RED);
  lcd.setCursor(PAD, 5);
  lcd.print(title);
}

void drawSoftButtons() {
#if SHOW_SOFT_BTNS
  int y = H - BUTTON_BAR_H;

  lcd.fillRect(0, y, W, BUTTON_BAR_H, C_BLACK);

  int bw = W / 4;

  for (int i = 0; i < 4; i++) {
    int x = i * bw + 3;
    bool enabled = (i < optionCount);

    uint16_t bg = enabled ? (i == selectedIdx ? C_YELLOW : C_BLUE) : C_BLACK;
    uint16_t fg = enabled ? (i == selectedIdx ? C_RED : C_WHITE) : C_GRAY;

    lcd.fillRoundRect(x, y + 4, bw - 6, BUTTON_BAR_H - 8, 6, bg);
    lcd.drawRoundRect(x, y + 4, bw - 6, BUTTON_BAR_H - 8, 6, C_WHITE);

    lcd.setFont(&fonts::efontJA_16);
    lcd.setTextColor(fg, bg);
    lcd.setCursor(x + 10, y + 12);
    lcd.print(String(i + 1) + ".");
  }
#endif
}

void drawScene() {
  lcd.fillScreen(C_BLACK);
  drawHeader();

  int sx = PAD;
  int sy = HEADER_H + 4;
  int sw = W - PAD * 2;

  lcd.setFont(&fonts::efontJA_12);
  int afterSceneY = printWrappedRetY(sx, sy, sw, sceneText, C_WHITE, C_BLACK);

  gOptionsTop = afterSceneY + 6;
  layoutOptions();

  for (int i = 0; i < optionCount; i++) {
    drawOptionLine(i, i == selectedIdx);
  }

  drawSoftButtons();
}

void drawThinking() {
  lcd.fillScreen(C_BLACK);
  drawHeader("Thinking...");

  lcd.setFont(&fonts::efontJA_16);
  lcd.setTextColor(C_WHITE, C_BLACK);
  lcd.setCursor(PAD, HEADER_H + 30);
  lcd.print("Please wait...");

  drawSoftButtons();
}

void drawGameOver() {
  lcd.fillScreen(C_BLACK);
  drawHeader("End of the adventure");

  lcd.setFont(&fonts::efontJA_16);
  lcd.setTextColor(C_GREEN, C_BLACK);
  lcd.setCursor(PAD, HEADER_H + 8);
  lcd.print("Epilogue:");

  lcd.setFont(&fonts::efontJA_12);
  printWrappedRetY(PAD, HEADER_H + 32, W - PAD * 2, sceneText, C_WHITE, C_BLACK);

  lcd.setFont(&fonts::efontJA_12);
  lcd.setTextColor(C_YELLOW, C_BLACK);
  lcd.setCursor(PAD, H - BUTTON_BAR_H - 16);
  lcd.print("START: new game   BTN_B: redraw screen");

  drawSoftButtons();
}

// ---------- LLM COMMUNICATION ----------
bool callLLM(
  const String& userMsg,
  String& outScene,
  String outOptions[4],
  int& outCount,
  bool& outEnd,
  String& outHint
) {
  String msg = "";
  msg.reserve(900);

  msg += "Context:\n";
  msg += contextLog;
  msg += "\n---\nTask:\n";
  msg += userMsg;
  msg += "\nReminder: respond STRICTLY as JSON without code blocks.\n";

  String raw = aiClient.chat(msg);

  if (raw.length() == 0) {
    aiFail("callLLM", "raw payload is empty (aiClient.chat returned empty string)");
    return false;
  }

  String json = extractJsonObject(raw);

  if (json.length() == 0) {
    aiFail("callLLM", "response does not contain a JSON object (no {...} found)");

    Serial.print("Raw (first 200): ");
    Serial.println(raw.substring(0, 200));

    return false;
  }

  JsonDocument doc;

  DeserializationError err = deserializeJson(doc, json);

  if (err) {
    aiFail("callLLM", String("deserializeJson failed: ") + err.c_str());

    Serial.print("JSON (first 400): ");
    Serial.println(json.substring(0, 400));

    return false;
  }

  outScene = doc["scene"] | "";
  outEnd = doc["end"] | false;
  outHint = doc["hint"] | "";

  outCount = 0;

  if (doc["options"].is<JsonArray>()) {
    JsonArray arr = doc["options"].as<JsonArray>();

    for (JsonVariant v : arr) {
      if (outCount < 4) {
        const char* opt = v.as<const char*>();

        if (opt != nullptr && strlen(opt) > 0) {
          outOptions[outCount++] = String(opt);
        }
      }
    }
  } else {
    aiFail("callLLM", "JSON parsed, but 'options' is missing or not an array");
    return false;
  }

  if (outScene.length() == 0) {
    aiFail("callLLM", "JSON parsed, but 'scene' is empty");
    return false;
  }

  if (outCount == 0 && !outEnd) {
    aiFail("callLLM", "JSON parsed, but no usable options were returned");
    return false;
  }

  contextLog += "\nDM: ";
  contextLog += outScene;

  if (outCount > 0) {
    contextLog += "\nDM options: ";

    for (int i = 0; i < outCount; i++) {
      contextLog += String(i + 1) + ") " + outOptions[i] + "  ";
    }
  }

  contextLog += "\n";
  trimContext();

  return true;
}

bool startNewGame() {
  contextLog = "";
  sceneText = "";
  optionCount = 0;
  selectedIdx = 0;

  String scene;
  String hint;
  int count = 0;
  bool isEnd = false;
  String opts[4];

  bool ok = callLLM(
    NEW_GAME_SEED,
    scene,
    opts,
    count,
    isEnd,
    hint
  );

  if (!ok) {
    aiFail("startNewGame", "callLLM returned false");
    return false;
  }

  sceneText = scene;
  optionCount = count;

  for (int i = 0; i < count; i++) {
    options[i] = opts[i];
  }

  selectedIdx = 0;

  gs = isEnd ? ST_GAMEOVER : ST_SHOW;

  return true;
}

bool advanceWithChoice(int humanChoiceIdx) {
  String chosen = (humanChoiceIdx >= 0 && humanChoiceIdx < optionCount)
                    ? options[humanChoiceIdx]
                    : String("Unknown");

  contextLog += String("Player: ") + String(humanChoiceIdx + 1) + ") " + chosen + "\n";
  trimContext();

  String userAsk =
    "Continue the story according to the player's choice. "
    "Choice: " + String(humanChoiceIdx + 1) + " - " + chosen + ". "
    "Return JSON using the same rules. Do not write reasoning. JSON only.";

  String scene;
  String hint;
  int count = 0;
  bool isEnd = false;
  String opts[4];

  bool ok = callLLM(
    userAsk,
    scene,
    opts,
    count,
    isEnd,
    hint
  );

  if (!ok) {
    aiFail("advanceWithChoice", "callLLM returned false");
    return false;
  }

  sceneText = scene;
  optionCount = count;

  for (int i = 0; i < count; i++) {
    options[i] = opts[i];
  }

  selectedIdx = 0;

  gs = isEnd ? ST_GAMEOVER : ST_SHOW;

  return true;
}

// ---------- SETUP / LOOP ----------
void setup() {
  Serial.begin(115200);

  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);
  pinMode(BTN_START, INPUT_PULLUP);

  pinMode(BTN_UD, INPUT);
  pinMode(BTN_LR, INPUT);

  lcd.init();
  lcd.setRotation(0);
  lcd.setTextWrap(false);
  lcd.fillScreen(C_BLACK);

  drawHeader("Connecting...");

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (!ledcAttach(SPEAKER_PIN, SPEAKER_LEDC_FREQ, SPEAKER_LEDC_RES)) {
    Serial.println("LEDC attach failed for the speaker.");
  }
#else
  ledcSetup(SPEAKER_LEDC_CHANNEL, SPEAKER_LEDC_FREQ, SPEAKER_LEDC_RES);
  ledcAttachPin(SPEAKER_PIN, SPEAKER_LEDC_CHANNEL);
#endif

  lcd.setFont(&fonts::efontJA_12);
  lcd.setTextColor(C_WHITE, C_BLACK);
  lcd.setCursor(PAD, HEADER_H + 10);
  lcd.print("WiFi: connecting to ");
  lcd.print(ssid);

  WiFi.begin(ssid, password);

  int tries = 0;

  while (WiFi.status() != WL_CONNECTED && tries < 60) {
    delay(250);
    lcd.print(".");
    tries++;
  }

  lcd.println();

  if (WiFi.status() != WL_CONNECTED) {
    lcd.setTextColor(C_RED, C_BLACK);
    lcd.println("Connection failed.");
  } else {
    lcd.setTextColor(C_GREEN, C_BLACK);
    lcd.print("IP: ");
    lcd.println(WiFi.localIP());
  }

  networkSelfTestSuite();

  aiClient.begin(platform, apiKey, model, customEndpoint);
  aiClient.setChatSystemRole(SYSTEM_PROMPT);
  aiClient.setChatTemperature(0.7);
  aiClient.setChatMaxTokens(380);

  if (startNewGame()) {
    drawScene();
    gs = ST_SHOW;
  } else {
    lcd.fillScreen(C_BLACK);
    drawHeader("LLM error");

    lcd.setFont(&fonts::efontJA_12);
    lcd.setTextColor(C_RED, C_BLACK);
    lcd.setCursor(PAD, HEADER_H + 20);
    lcd.print("Check the API, endpoint, and network.");

    gs = ST_IDLE;
  }
}

void loop() {
  unsigned long now = millis();

  int ud = readUD();

  bool A = (digitalRead(BTN_A) == LOW);
  bool B = (digitalRead(BTN_B) == LOW);
  bool START = (digitalRead(BTN_START) == LOW);

  if (gs == ST_SHOW || gs == ST_GAMEOVER) {
    if (START && edge(now)) {
      beep();
      drawThinking();

      if (startNewGame()) {
        drawScene();
      } else {
        drawGameOver();
      }

      return;
    }
  }

  if (gs == ST_SHOW) {
    if (isUp(ud) && edge(now)) {
      if (optionCount > 0) {
        int old = selectedIdx;
        selectedIdx = (selectedIdx - 1 + optionCount) % optionCount;

        updateSelectionHighlight(old, selectedIdx);
        beep(1500, 20);
      }

      return;
    }

    if (isDown(ud) && edge(now)) {
      if (optionCount > 0) {
        int old = selectedIdx;
        selectedIdx = (selectedIdx + 1) % optionCount;

        updateSelectionHighlight(old, selectedIdx);
        beep(1500, 20);
      }

      return;
    }

    if (A && edge(now)) {
      beep(2200, 40);
      drawThinking();

      gs = ST_WAIT_AI;

      bool ok = advanceWithChoice(selectedIdx);

      if (ok) {
        drawScene();
      } else {
        lcd.fillScreen(C_BLACK);
        drawHeader("LLM error");

        lcd.setFont(&fonts::efontJA_12);
        lcd.setTextColor(C_RED, C_BLACK);
        lcd.setCursor(PAD, HEADER_H + 20);
        lcd.print("Response failed. Press START.");

        gs = ST_IDLE;
      }

      return;
    }

    if (B && edge(now)) {
      beep(1000, 20);
      drawScene();
      return;
    }

    return;
  }

  if (gs == ST_GAMEOVER) {
    if (B && edge(now)) {
      beep(1000, 20);
      drawGameOver();
      return;
    }

    return;
  }

  if (gs == ST_IDLE) {
    if (START && edge(now)) {
      beep();
      drawThinking();

      if (startNewGame()) {
        drawScene();
      }
    }
  }
}