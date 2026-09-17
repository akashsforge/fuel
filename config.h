#pragma once

#define CAP_EMPTY   3900
#define CAP_FULL    1200
#define TANK_SIZE   40

#define FUEL_SENSOR_PIN   34
#define WATER_BUTTON_PIN  25
#define LED_PIN           2

#define LOW_FUEL_THRESHOLD  15     // % to start blinking
#define AIR_DROP_LITRES     3.0    // litres drop = air detected
#define AIR_DROP_TIME_MS    2000   // within this many ms
#define FUEL_NOISE_FILTER   0.5    // minimum change to count
