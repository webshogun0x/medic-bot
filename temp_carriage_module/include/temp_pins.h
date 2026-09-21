#ifndef TEMP_PINS_H
#define TEMP_PINS_H

#include <stdint.h>

/* =========================================================================
 * ESP-12F / ESP8266 Pinout Mapping
 * ========================================================================= */
// Shared I2C Bus (GY-906 IR Temp Sensor + 20x4 LCD Display)
#define PIN_I2C_SCL             5   // D1 (GPIO 5)
#define PIN_I2C_SDA             4   // D2 (GPIO 4)

// TM1637 4-Digit 7-Segment LED Display (Eye-Level Visual Confirmation)
#define PIN_TM1637_CLK          12  // D6 (GPIO 12)
#define PIN_TM1637_DIO          14  // D5 (GPIO 14)

// Forehead Proximity Ultrasonic Sensor
#define PIN_ULTRASONIC_TRIG     15  // D8 (GPIO 15)
#define PIN_ULTRASONIC_ECHO     13  // D7 (GPIO 13)

// Status LED (Active LOW on GPIO 2)
#define PIN_STATUS_LED          2

/* =========================================================================
 * Clinical & Proximity Calibration Constants
 * ========================================================================= */
// Optimal non-contact forehead distance (in cm)
#define TARGET_MIN_DIST_CM      2.5f
#define TARGET_MAX_DIST_CM      7.0f

// Plausible clinical body temperature range (in deg C)
#define MIN_VALID_TEMP_C        34.0f
#define MAX_VALID_TEMP_C        42.5f

// Forehead skin emissivity calibration offset
#define FOREHEAD_TEMP_OFFSET_C  0.8f

#endif // TEMP_PINS_H
