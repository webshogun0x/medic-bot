#ifndef WEIGHT_PINS_H
#define WEIGHT_PINS_H

#include <stdint.h>

/* =========================================================================
 * ESP-12F / ESP8266 GPIO Pin Mapping for HX711 Load Cell Amplifier
 * ========================================================================= */
// On ESP-12F:
// D2 is GPIO 4
// D1 is GPIO 5
#define PIN_HX711_DOUT          4   // D2 (GPIO 4)
#define PIN_HX711_SCK           5   // D1 (GPIO 5)

// Status LED (Active LOW on ESP-12F GPIO 2)
#define PIN_STATUS_LED          2

/* =========================================================================
 * Calibration & Threshold Parameters
 * ========================================================================= */
// Calibration factor (counts per kg) - adjust based on known calibration weight
#define DEFAULT_CALIBRATION_FACTOR (-7050.0f)

// Weight stability parameters
#define STABILITY_DELTA_KG      0.25f   // Standard deviation max for stable weight
#define PATIENT_DETECT_MIN_KG   15.0f   // Minimum threshold to confirm person on scale
#define TARE_THRESHOLD_KG       5.0f    // Only auto-tare if weight on platform is < 5kg

#endif // WEIGHT_PINS_H
