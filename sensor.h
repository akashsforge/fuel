#pragma once
#include "config.h"

// ═══════════════════════════════════════════════════
//   FUEL MONITOR — SENSOR LOGIC
// ═══════════════════════════════════════════════════

// ── State variables (used across loop() calls) ───
float previousLitres   = 0.0;
float addedLitres      = 0.0;
float lastStableLitres = 0.0;
bool  firstReading     = true;
bool  airDetected      = false;
unsigned long lastDropTime = 0;

// Reads raw ADC, clamps it, returns fuel % and litres
void readFuelSensor(float &fuelLitres, int &fuelPercent) {
  int rawADC = analogRead(FUEL_SENSOR_PIN);

  // Clamp so we never get negative or over-range values
  int clampedADC = constrain(rawADC,
                    min(CAP_FULL, CAP_EMPTY),
                    max(CAP_FULL, CAP_EMPTY));

  fuelPercent = map(clampedADC, CAP_EMPTY, CAP_FULL, 0, 100);
  fuelPercent = constrain(fuelPercent, 0, 100);

  fuelLitres = (float)(clampedADC - CAP_EMPTY)
             / (float)(CAP_FULL  - CAP_EMPTY)
             * TANK_SIZE;
}

// Detects how much fuel was added since last fill
void detectFuelAdded(float fuelLitres) {
  if (firstReading) {
    previousLitres   = fuelLitres;
    lastStableLitres = fuelLitres;
    firstReading     = false;
    return;
  }

  float diff = fuelLitres - previousLitres;

  if (diff > NOISE_FILTER_L) {
    // Fuel increased → someone added petrol
    addedLitres   += diff;
    previousLitres = fuelLitres;
    Serial.print(">> Fuel added: ");
    Serial.print(diff, 1);
    Serial.println("L  |  Total added this session: ");
    Serial.print(addedLitres, 1);
    Serial.println("L");
  } else if (diff < -NOISE_FILTER_L) {
    // Fuel decreased → consumption or drain
    previousLitres = fuelLitres;
  }
}

// Detects sudden drop = air bubble / air lock in line
void detectAir(float fuelLitres, int ledPin) {
  unsigned long now = millis();
  float dropAmount  = lastStableLitres - fuelLitres;

  if (dropAmount > AIR_DROP_LITRES && (now - lastDropTime) < AIR_DROP_TIME_MS) {
    airDetected = true;
    digitalWrite(ledPin, HIGH);   // solid ON = air warning
    Serial.println("!! AIR DETECTED in fuel line !!");
  } else {
    airDetected      = false;
    lastStableLitres = fuelLitres;
    lastDropTime     = now;
  }
}

// Controls LED: solid ON for air, blink for low fuel, OFF otherwise
void updateLED(int ledPin, int fuelPercent) {
  if (airDetected) {
    digitalWrite(ledPin, HIGH);
  } else if (fuelPercent <= LOW_FUEL_PERCENT) {
    digitalWrite(ledPin, (millis() / LED_BLINK_MS) % 2);
  } else {
    digitalWrite(ledPin, LOW);
  }
}

// Reads water sensor button (LOW = water detected)
bool readWaterSensor() {
  return (digitalRead(WATER_BUTTON_PIN) == LOW);
}

// Prints all readings to Serial Monitor for debugging/calibration
void printSerial(int count, int rawADC, int pct, float litres,
                 float added, bool water) {
  Serial.print("R#");      Serial.print(count);
  Serial.print(" | ADC:"); Serial.print(rawADC);
  Serial.print(" | Fuel:");Serial.print(pct);    Serial.print("%");
  Serial.print(" ");       Serial.print(litres, 1); Serial.print("L");
  Serial.print(" | Added:");Serial.print(added, 1); Serial.print("L");
  Serial.print(" | Water:");Serial.print(water ? "YES" : "No");
  Serial.print(" | Air:"); Serial.println(airDetected ? "YES" : "No");
}
