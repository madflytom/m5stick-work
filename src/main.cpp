#include <M5StickCPlus.h>

namespace {

constexpr uint32_t kRefreshMs = 125;
constexpr uint32_t kStaleMs = 5000;
constexpr size_t kBufferSize = 192;
constexpr uint8_t kBrightScreenLevel = 15;
constexpr uint8_t kDimScreenLevel = 10;

int screenWidth = 240;
int screenHeight = 135;

struct Telemetry {
  float cpuTempC = 0.0f;
  float wifiTempC = 0.0f;
  float pchTempC = 0.0f;
  float cpuLoadPct = 0.0f;
  float memUsedPct = 0.0f;
  float batt0Pct = -1.0f;
  float batt1Pct = -1.0f;
  float uptimeHours = 0.0f;
  int fanRpm = 0;
  uint32_t seq = 0;
  uint32_t lastUpdateMs = 0;
  bool valid = false;
};

Telemetry telemetry;
char inputBuffer[kBufferSize];
size_t inputLength = 0;
uint8_t currentPage = 0;

float parseFloatValue(const String &payload, const char *key, float fallback) {
  const String prefix = String(key) + "=";
  const int start = payload.indexOf(prefix);
  if (start < 0) {
    return fallback;
  }

  const int valueStart = start + prefix.length();
  int valueEnd = payload.indexOf(';', valueStart);
  if (valueEnd < 0) {
    valueEnd = payload.length();
  }

  return payload.substring(valueStart, valueEnd).toFloat();
}

int parseIntValue(const String &payload, const char *key, int fallback) {
  const String prefix = String(key) + "=";
  const int start = payload.indexOf(prefix);
  if (start < 0) {
    return fallback;
  }

  const int valueStart = start + prefix.length();
  int valueEnd = payload.indexOf(';', valueStart);
  if (valueEnd < 0) {
    valueEnd = payload.length();
  }

  return payload.substring(valueStart, valueEnd).toInt();
}

void applyTelemetry(const String &payload) {
  telemetry.cpuTempC = parseFloatValue(payload, "cpu", telemetry.cpuTempC);
  telemetry.wifiTempC = parseFloatValue(payload, "wifi", telemetry.wifiTempC);
  telemetry.pchTempC = parseFloatValue(payload, "pch", telemetry.pchTempC);
  telemetry.cpuLoadPct = parseFloatValue(payload, "load", telemetry.cpuLoadPct);
  telemetry.memUsedPct = parseFloatValue(payload, "mem", telemetry.memUsedPct);
  telemetry.batt0Pct = parseFloatValue(payload, "bat0", telemetry.batt0Pct);
  telemetry.batt1Pct = parseFloatValue(payload, "bat1", telemetry.batt1Pct);
  telemetry.uptimeHours = parseFloatValue(payload, "up", telemetry.uptimeHours);
  telemetry.fanRpm = parseIntValue(payload, "fan", telemetry.fanRpm);
  telemetry.seq = static_cast<uint32_t>(parseIntValue(payload, "seq", telemetry.seq));
  telemetry.lastUpdateMs = millis();
  telemetry.valid = true;
}

void readSerialTelemetry() {
  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());
    if (incoming == '\n') {
      inputBuffer[inputLength] = '\0';
      if (inputLength > 0) {
        applyTelemetry(String(inputBuffer));
      }
      inputLength = 0;
      continue;
    }

    if (incoming == '\r') {
      continue;
    }

    if (inputLength + 1 < kBufferSize) {
      inputBuffer[inputLength++] = incoming;
    } else {
      inputLength = 0;
    }
  }
}

void clearDisplay(uint16_t color = BLACK) {
  M5.Lcd.fillRect(0, 0, screenWidth, screenHeight, color);
}

void drawStatusBar(bool stale) {
  const uint16_t barColor = stale ? TFT_RED : TFT_DARKGREEN;
  M5.Lcd.fillRect(0, 0, screenWidth, 18, barColor);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(WHITE, barColor);
  M5.Lcd.setCursor(6, 5);
  M5.Lcd.print(stale ? "T470 OFFLINE" : "T470 LIVE");
  M5.Lcd.setCursor(screenWidth - 28, 5);
  M5.Lcd.printf("P%d", currentPage + 1);
}

void drawMetricBox(int x, int y, int w, int h, uint16_t borderColor, const char *label,
                   const String &value, uint8_t valueTextSize) {
  M5.Lcd.drawRoundRect(x, y, w, h, 6, borderColor);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_LIGHTGREY, BLACK);
  M5.Lcd.setCursor(x + 6, y + 4);
  M5.Lcd.print(label);
  M5.Lcd.setTextSize(valueTextSize);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(x + 6, y + 18);
  M5.Lcd.print(value);
}

void drawMetricGrid(uint16_t topLeftColor, const char *topLeftLabel, const String &topLeftValue,
                    uint16_t topRightColor, const char *topRightLabel, const String &topRightValue,
                    uint16_t bottomLeftColor, const char *bottomLeftLabel, const String &bottomLeftValue,
                    uint16_t bottomRightColor, const char *bottomRightLabel, const String &bottomRightValue) {
  const int outerMargin = 6;
  const int top = 24;
  const int gap = 6;
  const int boxWidth = (screenWidth - outerMargin * 2 - gap) / 2;
  const int boxHeight = (screenHeight - top - outerMargin - gap) / 2;

  drawMetricBox(outerMargin, top, boxWidth, boxHeight, topLeftColor, topLeftLabel, topLeftValue, 3);
  drawMetricBox(outerMargin + boxWidth + gap, top, boxWidth, boxHeight, topRightColor, topRightLabel, topRightValue, 3);
  drawMetricBox(outerMargin, top + boxHeight + gap, boxWidth, boxHeight, bottomLeftColor, bottomLeftLabel, bottomLeftValue, 3);
  drawMetricBox(outerMargin + boxWidth + gap, top + boxHeight + gap, boxWidth, boxHeight, bottomRightColor, bottomRightLabel, bottomRightValue, 3);
}

void drawHeader(bool stale) {
  drawStatusBar(stale);
}

void drawLine(int y, const char *label, const String &value, uint16_t color = WHITE) {
  M5.Lcd.setTextColor(TFT_CYAN, BLACK);
  M5.Lcd.setCursor(4, y);
  M5.Lcd.print(label);
  M5.Lcd.setTextColor(color, BLACK);
  M5.Lcd.setCursor(62, y);
  M5.Lcd.print(value);
}

void drawWaitingScreen() {
  clearDisplay();
  drawStatusBar(true);
  M5.Lcd.drawRoundRect(8, 28, screenWidth - 16, screenHeight - 36, 10, TFT_DARKGREY);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(18, 44);
  M5.Lcd.print("WAITING");
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(18, 78);
  M5.Lcd.print("Run bridge on T470");
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(18, 108);
  M5.Lcd.print("A: page  B: light");
}

void drawPage0(bool stale) {
  const uint16_t cpuColor = telemetry.cpuTempC >= 75.0f ? TFT_ORANGE : TFT_CYAN;
  drawMetricGrid(cpuColor, "CPU TEMP", String(telemetry.cpuTempC, 1) + "C",
                 TFT_BLUE, "FAN RPM", String(telemetry.fanRpm),
                 TFT_GREEN, "CPU LOAD", String(telemetry.cpuLoadPct, 0) + "%",
                 stale ? TFT_YELLOW : TFT_MAGENTA, "RAM USED", String(telemetry.memUsedPct, 0) + "%");
}

void drawPage1() {
  drawMetricGrid(TFT_BLUE, "WIFI TEMP", String(telemetry.wifiTempC, 1) + "C",
                 TFT_BLUE, "PCH TEMP", String(telemetry.pchTempC, 1) + "C",
                 TFT_ORANGE, "BAT0", telemetry.batt0Pct >= 0.0f ? String(telemetry.batt0Pct, 0) + "%" : "N/A",
                 TFT_ORANGE, "BAT1", telemetry.batt1Pct >= 0.0f ? String(telemetry.batt1Pct, 0) + "%" : "N/A");
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_LIGHTGREY, BLACK);
  M5.Lcd.setCursor(8, screenHeight - 10);
  M5.Lcd.printf("UP %.1fh  SEQ %lu", telemetry.uptimeHours, static_cast<unsigned long>(telemetry.seq));
}

void drawDashboard() {
  const bool stale = !telemetry.valid || (millis() - telemetry.lastUpdateMs) > kStaleMs;
  clearDisplay();
  drawHeader(stale);

  if (!telemetry.valid) {
    drawWaitingScreen();
    return;
  }

  if (currentPage == 0) {
    drawPage0(stale);
  } else {
    drawPage1();
  }
}

}  // namespace

void setup() {
  M5.begin();
  M5.Axp.ScreenBreath(kBrightScreenLevel);
  M5.Lcd.setRotation(1);
  screenWidth = M5.Lcd.width();
  screenHeight = M5.Lcd.height();
  M5.Lcd.setTextSize(1);
  clearDisplay();
  delay(30);
  clearDisplay();
  pinMode(M5_LED, OUTPUT);
  digitalWrite(M5_LED, HIGH);

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("M5StickC Plus laptop dashboard ready");

  drawWaitingScreen();
}

void loop() {
  static bool dimmed = false;
  static uint32_t lastRefresh = 0;

  M5.update();
  readSerialTelemetry();

  if (M5.BtnA.wasPressed()) {
    currentPage = (currentPage + 1) % 2;
    drawDashboard();
  }

  if (M5.BtnB.wasPressed()) {
    dimmed = !dimmed;
    M5.Axp.ScreenBreath(dimmed ? kDimScreenLevel : kBrightScreenLevel);
    drawDashboard();
  }

  if (millis() - lastRefresh >= kRefreshMs) {
    drawDashboard();
    lastRefresh = millis();
    digitalWrite(M5_LED, telemetry.valid && (millis() - telemetry.lastUpdateMs) <= kStaleMs ? LOW : HIGH);
  }

  delay(20);
}