#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define CAP_EMPTY 4095
#define CAP_FULL 0
#define TANK_SIZE 40

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_SDA 21
#define OLED_SCL 22

#define FUEL_SENSOR_PIN 34
#define WATER_BUTTON_PIN 25
#define LED_PIN 2

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── WiFi (Wokwi's built-in simulated network) ───────────────────────
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// ── n8n webhook ──────────────────────────────────────────────────────
// Use the TEST url while you're clicking "Test this trigger" in n8n.
// Switch to the PRODUCTION url once the workflow is toggled Active.
const char* n8nWebhookUrl = "https://akashpoojari.app.n8n.cloud/webhook/fuel-monitor";

int fuelPercent = 0;
float fuelLitres = 0.0;
float previousLitres = 0.0;
float addedLitres = 0.0;      // litres added this session
float sessionStartL = 0.0;    // litres at start of fill
bool waterDetected = false;
bool airDetected = false;
bool firstReading = true;
bool fillingActive = false;   // true while fuel is being added
int readingCount = 0;

// ── Air-detection rate tracking ─────────────────────────────────────
unsigned long lastRateCheckTime = 0;
float lastRateCheckLitres = 0.0;
unsigned long airDetectedUntil = 0;
const unsigned long RATE_CHECK_INTERVAL_MS = 300;
const float AIR_DROP_RATE_LPS = 20.0;

// ── n8n send tracking ────────────────────────────────────────────────
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL_MS = 5000; // heartbeat send every 5s
bool prevAirState = false;
bool prevWaterState = false;

// ── Display helpers ──────────────────────────────────────────────────
void drawFuelBar(int pct) {
  display.drawRect(0, 54, 128, 10, SSD1306_WHITE);
  int fillWidth = map(pct, 0, 100, 0, 126);
  display.fillRect(1, 55, fillWidth, 8, SSD1306_WHITE);
}

String getFuelStatus(int pct) {
  if (pct <= 10) return "!! CRITICAL !!";
  if (pct <= 25) return "LOW - Refuel!";
  if (pct <= 50) return "HALF TANK";
  if (pct <= 75) return "GOOD";
  return "FULL TANK";
}

void updateDisplay(int pct, float litres, float prevL, float added, bool water, bool air) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(15, 0);
  display.print("FUEL MONITOR v3.1");
  display.drawLine(0, 9, 128, 9, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 11);
  display.print(pct);
  display.print("%");
  display.setTextSize(1);
  display.setCursor(75, 11);
  display.print(litres, 1);
  display.print("L");

  display.setCursor(0, 27);
  display.print("Prev:");
  display.print(prevL, 1);
  display.print("L");

  display.setCursor(70, 27);
  display.print("+");
  display.print(added, 1);
  display.print("L");

  display.setCursor(0, 38);
  if (air) {
    display.print("!! AIR IN LINE !!");
  } else if (water) {
    display.print("!! WATER DETECT !!");
  } else {
    display.print(getFuelStatus(pct));
  }

  if (fillingActive) {
    display.setCursor(0, 47);
    display.print(">> FILLING...");
  }

  // WiFi status indicator, top-right corner
  display.setCursor(115, 0);
  display.print(WiFi.status() == WL_CONNECTED ? "W" : "X");

  drawFuelBar(pct);
  display.display();
}

// ── n8n webhook sender ────────────────────────────────────────────────
void sendToN8n(int pct, float litres, bool water, bool air) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected — skipping n8n send");
    return;
  }

  HTTPClient http;
  http.begin(n8nWebhookUrl);
  http.addHeader("Content-Type", "application/json");

  String payload = "{";
  payload += "\"fuelPercent\":" + String(pct) + ",";
  payload += "\"fuelLitres\":" + String(litres, 1) + ",";
  payload += "\"water\":" + String(water ? "true" : "false") + ",";
  payload += "\"air\":" + String(air ? "true" : "false");
  payload += "}";

  int httpCode = http.POST(payload);
  Serial.print("n8n POST -> ");
  Serial.print(payload);
  Serial.print(" | response code: ");
  Serial.println(httpCode);

  http.end();
}

// ── Setup ────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(WATER_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found!");
    while (true);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(15, 10);
  display.println("FUEL MONITOR");
  display.setCursor(25, 25);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
    delay(300);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi FAILED to connect — running offline.");
  }

  display.clearDisplay();
  display.setCursor(15, 10);
  display.println("FUEL MONITOR");
  display.setCursor(25, 25);
  display.println(WiFi.status() == WL_CONNECTED ? "WiFi OK" : "WiFi FAILED");
  display.display();
  delay(1500);

  Serial.println("=====================================");
  Serial.println(" FUEL MONITOR v3.1 — Ready!");
  Serial.println("=====================================");

  lastSendTime = millis();
}

// ── Loop ─────────────────────────────────────────────────────────────
void loop() {
  int rawADC = analogRead(FUEL_SENSOR_PIN);
  int clampedADC = constrain(rawADC,
                              min(CAP_FULL, CAP_EMPTY),
                              max(CAP_FULL, CAP_EMPTY));

  fuelPercent = map(clampedADC, CAP_EMPTY, CAP_FULL, 0, 100);
  fuelPercent = constrain(fuelPercent, 0, 100);

  fuelLitres = (float)(clampedADC - CAP_EMPTY)
               / (float)(CAP_FULL - CAP_EMPTY)
               * TANK_SIZE;
  fuelLitres = constrain(fuelLitres, 0.0, (float)TANK_SIZE);

  // ── Fuel tracking ────────────────────────────────────────────────
  if (firstReading) {
    previousLitres = fuelLitres;
    sessionStartL = fuelLitres;
    lastRateCheckLitres = fuelLitres;
    lastRateCheckTime = millis();
    firstReading = false;
    Serial.print("Starting fuel level: ");
    Serial.print(fuelLitres, 1);
    Serial.println("L");
  } else {
    float diff = fuelLitres - previousLitres;

    if (diff > 0.5) {
      if (!fillingActive) {
        fillingActive = true;
        sessionStartL = previousLitres;
        Serial.println("-------------------------------------");
        Serial.print("FILL STARTED at: ");
        Serial.print(sessionStartL, 1);
        Serial.println("L");
      }
      addedLitres += diff;
      previousLitres = fuelLitres;
      Serial.print(" +"); Serial.print(diff, 1);
      Serial.print("L added | Now: "); Serial.print(fuelLitres, 1);
      Serial.print("L | Total added: "); Serial.print(addedLitres, 1);
      Serial.println("L");
    } else if (diff < -5.0) {
      if (fillingActive) {
        Serial.println("-------------------------------------");
        Serial.print("FILL COMPLETE: ");
        Serial.print(sessionStartL, 1);
        Serial.print("L -> ");
        Serial.print(previousLitres, 1);
        Serial.print("L | Added: ");
        Serial.print(addedLitres, 1);
        Serial.println("L");
        Serial.println("-------------------------------------");
      }
      fillingActive = false;
      addedLitres = 0.0;
      previousLitres = fuelLitres;
      Serial.println("New session started. Counter reset.");
    } else if (diff < -0.5) {
      fillingActive = false;
      previousLitres = fuelLitres;
    }
  }

  // ── Air detection: rate of change (L/s) over a fixed window ──────
  unsigned long now = millis();
  if (now - lastRateCheckTime >= RATE_CHECK_INTERVAL_MS) {
    float dropAmount = lastRateCheckLitres - fuelLitres;
    float elapsedSec = (now - lastRateCheckTime) / 1000.0;
    float dropRate = dropAmount / elapsedSec;

    if (dropRate > AIR_DROP_RATE_LPS) {
      airDetected = true;
      airDetectedUntil = now + 3000;
      Serial.print("!! AIR DETECTED — rate: ");
      Serial.print(dropRate, 1);
      Serial.println(" L/s !!");
    }

    lastRateCheckLitres = fuelLitres;
    lastRateCheckTime = now;
  }

  if (airDetected && now > airDetectedUntil) {
    airDetected = false;
  }

  // ── Water detection ────────────────────────────────────────────────
  waterDetected = (digitalRead(WATER_BUTTON_PIN) == LOW);
  if (waterDetected) Serial.println("!! WATER DETECTED !!");

  // ── LED control ─────────────────────────────────────────────────────
  if (airDetected) {
    digitalWrite(LED_PIN, HIGH);
  } else if (fuelPercent <= 15) {
    digitalWrite(LED_PIN, (millis() / 500) % 2);
  } else {
    digitalWrite(LED_PIN, LOW);
  }

  // ── Update OLED ──────────────────────────────────────────────────────
  updateDisplay(fuelPercent, fuelLitres, sessionStartL,
                addedLitres, waterDetected, airDetected);

  // ── Send to n8n ──────────────────────────────────────────────────────
  // Sends immediately on air/water state change (event-driven),
  // and also as a periodic heartbeat every SEND_INTERVAL_MS.
  bool stateChanged = (airDetected != prevAirState) || (waterDetected != prevWaterState);
  bool heartbeatDue = (now - lastSendTime >= SEND_INTERVAL_MS);

  if (stateChanged || heartbeatDue) {
    sendToN8n(fuelPercent, fuelLitres, waterDetected, airDetected);
    lastSendTime = now;
    prevAirState = airDetected;
    prevWaterState = waterDetected;
  }

  // ── Serial summary ───────────────────────────────────────────────────
  readingCount++;
  Serial.print("R#"); Serial.print(readingCount);
  Serial.print(" | ADC:"); Serial.print(rawADC);
  Serial.print(" | Now:"); Serial.print(fuelLitres,1); Serial.print("L");
  Serial.print(" ("); Serial.print(fuelPercent); Serial.print("%)");
  Serial.print(" | Prev:"); Serial.print(previousLitres,1); Serial.print("L");
  Serial.print(" | Added:"); Serial.print(addedLitres,1); Serial.print("L");
  Serial.print(" | Water:"); Serial.print(waterDetected ? "YES":"No");
  Serial.print(" | Air:"); Serial.println(airDetected ? "YES":"No");

  delay(100);
}