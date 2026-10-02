#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "cs2logo.h"
#include "secrets.h"

// ---------------------------------------------------------------------------
// Config — same stats the web frontend shows: KDR, Wins, MVPs (no chart)
// ---------------------------------------------------------------------------
const unsigned long CURRENT_REFRESH_MS = 10000;

// Mirror of frontend/src/styles.css palette (dark theme)
constexpr uint32_t COL_BG = 0x0000;      // #000000
constexpr uint32_t COL_CARD = 0x2124;    // #242424
constexpr uint32_t COL_TEXT = 0xE71C;    // #e8eaf0
constexpr uint32_t COL_MUTED = 0x94B2;   // #8b93a7
constexpr uint32_t COL_GREEN = 0x4F64;   // #4ade80
constexpr uint32_t COL_BLUE = 0x5DFF;    // #60a5fa
constexpr uint32_t COL_GOLD = 0xFD40;    // #fbbf24
constexpr uint32_t COL_RED = 0xF9A6;     // #f87171

TFT_eSPI tft;

struct Snapshot {
  float kdr = 0;
  uint32_t kills = 0;
  uint32_t deaths = 0;
  uint32_t wins = 0;
  uint32_t mvps = 0;
  uint32_t matchesPlayed = 0;
  uint32_t matchesWon = 0;
};

Snapshot snapshot;
bool hasData = false;
bool hasError = false;
String errorMessage;
unsigned long lastFetch = 0;

// ---------------------------------------------------------------------------
// Chart view (toggled by tapping the screen)
// ---------------------------------------------------------------------------
enum class View : uint8_t { Dashboard, Chart };
View currentView = View::Dashboard;

constexpr int KDR_MAX_SAMPLES = 512;
float kdrSamples[KDR_MAX_SAMPLES];
int kdrSampleCount = 0;
unsigned long lastHistoryFetch = 0;
constexpr unsigned long HISTORY_REFRESH_MS = 60000;

// ---------------------------------------------------------------------------
// XPT2046 resistive touch (bit-banged on the touch-only SPI pins)
// ---------------------------------------------------------------------------
constexpr int T_CS = 33;
constexpr int T_CLK = 25;
constexpr int T_DIN = 32;
constexpr int T_DOUT = 39;

void touchInit() {
  pinMode(T_CS, OUTPUT);
  digitalWrite(T_CS, HIGH);
  pinMode(T_CLK, OUTPUT);
  digitalWrite(T_CLK, LOW);
  pinMode(T_DIN, OUTPUT);
  digitalWrite(T_DIN, LOW);
  pinMode(T_DOUT, INPUT);
}

// Port of Freenove's TFT_Touch _ReadAxis: cmd 0x90 = axis A, 0xD0 = axis B
uint16_t touchReadAxis(bool axis) {
  digitalWrite(T_CS, LOW);
  const uint8_t cmd = axis ? 0x90 : 0xD0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(T_DIN, (cmd >> i) & 1);
    digitalWrite(T_CLK, HIGH);
    digitalWrite(T_CLK, LOW);
  }
  digitalWrite(T_CLK, HIGH);
  digitalWrite(T_CLK, LOW);
  uint16_t v = 0;
  for (int i = 11; i >= 0; i--) {
    v |= (uint16_t)digitalRead(T_DOUT) << i;
    digitalWrite(T_CLK, HIGH);
    digitalWrite(T_CLK, LOW);
  }
  digitalWrite(T_CS, HIGH);
  digitalWrite(T_DIN, LOW);
  return v;
}

// Freenove calibration (setCal 527,3552,683,3464,320,240, rotation 1)
bool screenTouched() {
  const uint16_t xr = touchReadAxis(true);
  const uint16_t yr = touchReadAxis(false);
  return xr > 6 && xr < 4090 && yr > 6 && yr < 4090;
}

// ---------------------------------------------------------------------------
// Layout: 320 wide (landscape), 3 stat cards side by side like the web grid
// ---------------------------------------------------------------------------
constexpr int16_t SCREEN_W = 320;
constexpr int16_t SCREEN_H = 240;
constexpr int16_t HEADER_H = 30;

struct Card {
  const char* label;
  uint32_t accent;
};

Card cards[] = {
  {"KDR", COL_GREEN},
  {"Wins", COL_BLUE},
  {"MVPs", COL_GOLD},
};
constexpr int CARD_COUNT = 3;
constexpr int CARD_GAP = 8;
constexpr int CARD_X0 = 10;
constexpr int CARD_W = (SCREEN_W - 2 * CARD_X0 - (CARD_COUNT - 1) * CARD_GAP) / CARD_COUNT + 1;
constexpr int CARD_Y = HEADER_H + 20;
constexpr int CARD_H = 115;

// ---------------------------------------------------------------------------
void drawStatusDot(bool up) {
  tft.fillSmoothCircle(SCREEN_W - 12, 15, 4, up ? COL_GREEN : COL_RED, up ? COL_GREEN : COL_RED);
}

void drawHeader() {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, COL_BG);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextDatum(ML_DATUM);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString("OSIRIS DREAMS", 10, HEADER_H / 2 + 6);
  drawStatusDot(!hasError);
}

void drawCardFrame(int index, uint32_t accent, bool pressed = false) {
  const int x = CARD_X0 + index * (CARD_W + CARD_GAP);
  const uint32_t bg = pressed ? COL_BG : COL_CARD;
  tft.fillSmoothRoundRect(x, CARD_Y, CARD_W, CARD_H, 8, bg, COL_BG);

  // Top accent strip (match accent-green/blue/gold cards on the web UI)
  tft.fillSmoothRoundRect(x + 10, CARD_Y + 14, 22, 4, 2, accent, bg);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COL_MUTED, bg);
  tft.setTextPadding(CARD_W - 20);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.drawString(cards[index].label, x + 10, CARD_Y + 34);

  // Reset the value area
  tft.fillRect(x + 1, CARD_Y + 46, CARD_W - 2, 32, bg);
  tft.setTextColor(accent, bg);
  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);

  char value[16];
  if (!hasData) {
    snprintf(value, sizeof(value), "--");
  } else if (index == 0) {
    snprintf(value, sizeof(value), "%.2f", snapshot.kdr);
  } else if (index == 1) {
    if (snapshot.matchesPlayed > 0) {
      snprintf(value, sizeof(value), "%u%%",
               (unsigned)((snapshot.matchesWon * 100UL + snapshot.matchesPlayed / 2) / snapshot.matchesPlayed));
    } else {
      snprintf(value, sizeof(value), "--");
    }
  } else {
    snprintf(value, sizeof(value), "%lu", (unsigned long)snapshot.mvps);
  }
  tft.drawString(value, x + 10, CARD_Y + 52);

  // Detail line (same details as the web StatCard)
  tft.setTextColor(COL_MUTED, bg);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.setTextPadding(CARD_W - 20);
  char detail[36];
  if (!hasData) {
    detail[0] = '\0';
  } else if (index == 0) {
    snprintf(detail, sizeof(detail), "%lu/%lu",
             (unsigned long)snapshot.kills, (unsigned long)snapshot.deaths);
  } else if (index == 1) {
    snprintf(detail, sizeof(detail), "%lu wins", (unsigned long)snapshot.wins);
  } else {
    if (snapshot.matchesPlayed > 0) {
      snprintf(detail, sizeof(detail), "%.2f",
               (float)snapshot.mvps / snapshot.matchesPlayed);
    } else {
      detail[0] = '\0';
    }
  }
  tft.drawString(detail, x + 10, CARD_Y + CARD_H - 14);
}

void drawFooter() {
  tft.fillRect(0, CARD_Y + CARD_H + 16, SCREEN_W, SCREEN_H - (CARD_Y + CARD_H + 14), COL_BG);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.setFreeFont(&FreeSans9pt7b);
  if (hasError) {
    tft.setTextColor(COL_RED, COL_BG);
    String msg = "Error: " + errorMessage;
    tft.setTextPadding(SCREEN_W - 24);
    tft.drawString(msg, 12, SCREEN_H - 18);
  } else {
    tft.setTextColor(COL_MUTED, COL_BG);
    tft.setTextPadding(SCREEN_W - 24);
    if (!hasData) {
      tft.drawString("Waiting for first snapshot...", 12, SCREEN_H - 18);
    }
  }
}

void drawLogo() {
  // Official CS2 wordmark as bitmap, centered below the cards
  const int x = (SCREEN_W - CS2_LOGO_W) / 2;
  const int y = CARD_Y + CARD_H + 18;
  tft.pushImage(x, y, CS2_LOGO_W, CS2_LOGO_H, cs2logo);
}

void drawDashboard() {
  drawHeader();
  for (int i = 0; i < CARD_COUNT; i++) {
    drawCardFrame(i, cards[i].accent);
  }
  drawFooter();
  drawLogo();
}

// ---------------------------------------------------------------------------
// Full screen K/D chart
// ---------------------------------------------------------------------------
constexpr int PLOT_X = 42;
constexpr int PLOT_Y = 40;
constexpr int PLOT_W = SCREEN_W - PLOT_X - 12;
constexpr int PLOT_H = 150;

void drawChart() {
  tft.fillScreen(COL_BG);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString("K/D HISTORY", 10, 13);
  drawStatusDot(!hasError);

  if (kdrSampleCount < 2) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_MUTED, COL_BG);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.drawString("No history yet", SCREEN_W / 2, SCREEN_H / 2);
    tft.setTextDatum(ML_DATUM);
    return;
  }

  float lo = kdrSamples[0], hi = kdrSamples[0];
  float rawLo = kdrSamples[0], rawHi = kdrSamples[0];
  double sum = 0;
  for (int i = 0; i < kdrSampleCount; i++) {
    if (kdrSamples[i] < lo) lo = kdrSamples[i];
    if (kdrSamples[i] > hi) hi = kdrSamples[i];
    sum += kdrSamples[i];
  }
  rawLo = lo;
  rawHi = hi;
  if (hi - lo < 0.2f) {  // flat data: give the line some room
    const float mid = (hi + lo) / 2;
    lo = mid - 0.1f;
    hi = mid + 0.1f;
  } else {
    const float pad = (hi - lo) * 0.08f;
    lo -= pad;
    hi += pad;
  }
  const float avg = sum / kdrSampleCount;

  // Grid + y labels
  tft.setTextFont(2);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.setTextDatum(MR_DATUM);
  for (int g = 0; g <= 4; g++) {
    const int y = PLOT_Y + PLOT_H - PLOT_H * g / 4;
    const float v = lo + (hi - lo) * g / 4;
    tft.drawFastHLine(PLOT_X, y, PLOT_W, COL_CARD);
    char label[8];
    snprintf(label, sizeof(label), "%.2f", v);
    tft.drawString(label, PLOT_X - 4, y);
  }

  // Line
  auto mapY = [&](float v) -> int {
    return PLOT_Y + (int)((hi - v) / (hi - lo) * PLOT_H + 0.5f);
  };
  int px = PLOT_X;
  int py = mapY(kdrSamples[0]);
  for (int i = 1; i < kdrSampleCount; i++) {
    const int nx = PLOT_X + (int)((float)i / (kdrSampleCount - 1) * PLOT_W + 0.5f);
    const int ny = mapY(kdrSamples[i]);
    tft.drawLine(px, py, nx, ny, COL_BLUE);
    px = nx;
    py = ny;
  }

  // Latest value marker + label
  tft.fillSmoothCircle(px, py, 3, COL_GREEN, COL_GREEN);
  char last[10];
  snprintf(last, sizeof(last), "%.2f", kdrSamples[kdrSampleCount - 1]);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COL_GREEN, COL_BG);
  tft.setTextFont(2);
  tft.drawString(last, (px < SCREEN_W - 60) ? px + 6 : px - 54, py - 8);

  // Footer: min / avg / max
  char stats[48];
  snprintf(stats, sizeof(stats), "min %.2f   avg %.2f   max %.2f", rawLo, avg, rawHi);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString(stats, SCREEN_W / 2, SCREEN_H - 14);
  tft.setTextFont(1);
}

void drawAll() {
  tft.fillScreen(COL_BG);
  if (currentView == View::Dashboard) {
    drawDashboard();
  } else {
    drawChart();
  }
}

// ---------------------------------------------------------------------------
bool fetchCurrent(HTTPClient& http, JsonDocument& doc) {
  String url = String(API_BASE) + "/api/current";
  http.begin(url);
  http.setTimeout(5000);
  const int code = http.GET();
  if (code != 200) {
    errorMessage = String(code) + " " + http.errorToString(code);
    return false;
  }
  const String body = http.getString();
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    errorMessage = err.c_str();
    return false;
  }
  if (doc.isNull()) {  // backend returns null before the first snapshot
    hasData = false;
    return true;
  }
  snapshot.kdr = doc["kdr"] | 0.0f;
  snapshot.kills = doc["kills"] | 0;
  snapshot.deaths = doc["deaths"] | 0;
  snapshot.wins = doc["wins"] | 0;
  snapshot.mvps = doc["mvps"] | 0;
  snapshot.matchesPlayed = doc["matchesPlayed"] | 0;
  snapshot.matchesWon = doc["matchesWon"] | 0;
  hasData = true;
  return true;
}

bool fetchHistory(HTTPClient& http) {
  String url = String(API_BASE) + "/api/history?limit=2000";
  http.begin(url);
  http.setTimeout(8000);
  const int code = http.GET();
  if (code != 200) return false;
  const String body = http.getString();

  JsonDocument filter;  // only keep kdr values to save RAM
  filter[0]["kdr"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) return false;

  JsonArrayConst arr = doc.as<JsonArrayConst>();
  const int n = arr.size();
  kdrSampleCount = 0;
  if (n <= 0) return true;
  const int stride = (n + KDR_MAX_SAMPLES - 1) / KDR_MAX_SAMPLES;
  for (int i = 0; i < n; i += stride) {
    kdrSamples[kdrSampleCount++] = arr[i]["kdr"] | 0.0f;
  }
  return true;
}

void refresh() {
  HTTPClient http;
  JsonDocument doc;
  const bool ok = fetchCurrent(http, doc);
  http.end();

  if (ok) {
    hasError = false;
    errorMessage = "";
  } else {
    hasError = true;
  }

  const bool historyDue = lastHistoryFetch == 0 || millis() - lastHistoryFetch >= HISTORY_REFRESH_MS;
  if (historyDue) {
    HTTPClient httpHist;
    fetchHistory(httpHist);  // chart failure doesn't flag hasError
    httpHist.end();
    lastHistoryFetch = millis();
  }

  if (currentView == View::Dashboard) {
    drawStatusDot(!hasError);
    for (int i = 0; i < CARD_COUNT; i++) {
      drawCardFrame(i, cards[i].accent);
    }
    drawFooter();
    drawLogo();
  } else {
    drawChart();
  }
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString("CS2 Stats", SCREEN_W / 2, 90);
  tft.setTextDatum(ML_DATUM);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Connecting to WiFi", 12, 140);
  uint32_t start = millis();
  tft.setTextDatum(TL_DATUM);
  tft.setCursor(190, 145);
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    tft.print('.');
    delay(400);
  }
  if (WiFi.status() != WL_CONNECTED) {
    tft.setTextColor(COL_RED, COL_BG);
    tft.drawString("WiFi failed - rebooting", 12, 165);
    delay(3000);
    ESP.restart();
  }
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setSwapBytes(true);  // required for pushImage color order
  tft.setRotation(1);  // landscape 320x240
  tft.fillScreen(COL_BG);
  touchInit();

  drawAll();
  connectWifi();
  drawAll();
  refresh();
  lastFetch = millis();
}

void loop() {
  // Tap anywhere toggles between dashboard and K/D chart
  if (screenTouched()) {
    unsigned long start = millis();
    while (screenTouched() && millis() - start < 2000) {
      delay(20);
    }
    currentView = (currentView == View::Dashboard) ? View::Chart : View::Dashboard;
    drawAll();
  }

  const unsigned long now = millis();
  if (now - lastFetch >= CURRENT_REFRESH_MS) {
    lastFetch = now;
    if (WiFi.status() != WL_CONNECTED) {
      connectWifi();
      drawAll();
    }
    refresh();
  }
  delay(50);
}
