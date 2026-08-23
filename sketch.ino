#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define CAP_EMPTY   4095
#define CAP_FULL    0
#define TANK_SIZE   40

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_SDA      21
#define OLED_SCL      22
#define FUEL_SENSOR_PIN   34
#define WATER_BUTTON_PIN  25
#define LED_PIN           2

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int   fuelPercent      = 0;
float fuelLitres       = 0.0;
float previousLitres   = 0.0;
float addedLitres      = 0.0;       // litres added this session
float sessionStartL    = 0.0;       // litres at start of fill
float lastStableLitres = 0.0;
bool  waterDetected    = false;
bool  airDetected      = false;
bool  firstReading     = true;
bool  fillingActive    = false;     // true while fuel is being added
int   readingCount     = 0;
unsigned long lastDropTime = 0;

// ── Display helpers ───────────────────────────────────────────────────────────
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

  // Title
  display.setCursor(15, 0);
  display.print("FUEL MONITOR v3.0");
  display.drawLine(0, 9, 128, 9, SSD1306_WHITE);

  // Current fuel — big % left, litres right
  display.setTextSize(2);
  display.setCursor(0, 11);
  display.print(pct);
  display.print("%");
  display.setTextSize(1);
  display.setCursor(75, 11);
  display.print(litres, 1);
  display.print("L");

  // Previous litres
  display.setCursor(0, 27);
  display.print("Prev:");
  display.print(prevL, 1);
  display.print("L");

  // Added this session
  display.setCursor(70, 27);
  display.print("+");
  display.print(added, 1);
  display.print("L");

  // Status line
  display.setCursor(0, 38);
  if (air) {
    display.print("!! AIR IN LINE !!");
  } else if (water) {
    display.print("!! WATER DETECT !!");
  } else {
    display.print(getFuelStatus(pct));
  }

  // Filling indicator
  if (fillingActive) {
    display.setCursor(0, 47);
    display.print(">> FILLING...");
  }

  drawFuelBar(pct);
  display.display();
}

// ── Setup ─────────────────────────────────────────────────────────────────────
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
  display.println("Initializing...");
  display.display();
  delay(2000);

  Serial.println("=====================================");
  Serial.println("   FUEL MONITOR v3.0 — Ready!");
  Serial.println("=====================================");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
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

  // ── Fuel tracking ───────────────────────────────────────────────────
  if (firstReading) {
    previousLitres   = fuelLitres;
    sessionStartL    = fuelLitres;
    lastStableLitres = fuelLitres;
    firstReading     = false;
    Serial.print("Starting fuel level: ");
    Serial.print(fuelLitres, 1);
    Serial.println("L");

  } else {
    float diff = fuelLitres - previousLitres;

    if (diff > 0.5) {
      // ── Fuel being added ──
      if (!fillingActive) {
        fillingActive = true;
        sessionStartL = previousLitres;   // record where fill started
        Serial.println("-------------------------------------");
        Serial.print("FILL STARTED at: ");
        Serial.print(sessionStartL, 1);
        Serial.println("L");
      }
      addedLitres   += diff;
      previousLitres = fuelLitres;

      Serial.print("  +"); Serial.print(diff, 1);
      Serial.print("L added | Now: "); Serial.print(fuelLitres, 1);
      Serial.print("L | Total added: "); Serial.print(addedLitres, 1);
      Serial.println("L");

    } else if (diff < -5.0) {
      // ── Big drop = new session ──
      if (fillingActive) {
        Serial.println("-------------------------------------");
        Serial.print("FILL COMPLETE: ");
        Serial.print(sessionStartL, 1);
        Serial.print("L → ");
        Serial.print(previousLitres, 1);
        Serial.print("L | Added: ");
        Serial.print(addedLitres, 1);
        Serial.println("L");
        Serial.println("-------------------------------------");
      }
      fillingActive  = false;
      addedLitres    = 0.0;
      previousLitres = fuelLitres;
      Serial.println("New session started. Counter reset.");

    } else if (diff < -0.5) {
      // ── Normal consumption ──
      fillingActive  = false;
      previousLitres = fuelLitres;
    }
  }

  // ── Air detection ───────────────────────────────────────────────────
  unsigned long now = millis();
  float dropAmount  = lastStableLitres - fuelLitres;

  if (dropAmount > 3.0) {
    if ((now - lastDropTime) < 2000) {
      airDetected = true;
      Serial.println("!! AIR DETECTED in fuel line !!");
    }
    lastDropTime = now;
  } else {
    airDetected      = false;
    lastStableLitres = fuelLitres;
  }

  // ── Water detection ─────────────────────────────────────────────────
  waterDetected = (digitalRead(WATER_BUTTON_PIN) == LOW);
  if (waterDetected) Serial.println("!! WATER DETECTED !!");

  // ── LED control ─────────────────────────────────────────────────────
  if (airDetected) {
    digitalWrite(LED_PIN, HIGH);                      // solid = air
  } else if (fuelPercent <= 15) {
    digitalWrite(LED_PIN, (millis() / 500) % 2);     // blink = low fuel
  } else {
    digitalWrite(LED_PIN, LOW);                       // off = normal
  }

  // ── Update OLED ─────────────────────────────────────────────────────
  updateDisplay(fuelPercent, fuelLitres, sessionStartL,
                addedLitres, waterDetected, airDetected);

  // ── Serial summary ──────────────────────────────────────────────────
  readingCount++;
  Serial.print("R#"); Serial.print(readingCount);
  Serial.print(" | ADC:"); Serial.print(rawADC);
  Serial.print(" | Now:"); Serial.print(fuelLitres,1); Serial.print("L");
  Serial.print(" ("); Serial.print(fuelPercent); Serial.print("%)");
  Serial.print(" | Prev:"); Serial.print(previousLitres,1); Serial.print("L");
  Serial.print(" | Added:"); Serial.print(addedLitres,1); Serial.print("L");
  Serial.print(" | Water:"); Serial.print(waterDetected ? "YES":"No");
  Serial.print(" | Air:"); Serial.println(airDetected ? "YES":"No");

  delay(500);
}
