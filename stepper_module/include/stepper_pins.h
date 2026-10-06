#ifndef STEPPER_PINS_H
#define STEPPER_PINS_H

#include <stdint.h>

/* =========================================================================
 * TB6600 Stepper Motor Driver Pinout (ESP-WROOM-32)
 * ========================================================================= */
#define PIN_STEPPER_PUL         18  // Pulse / Step Pin
#define PIN_STEPPER_DIR         19  // Direction Pin
#define PIN_STEPPER_ENA         21  // Enable Pin (LOW = Driver Energized / Enabled)

/* =========================================================================
 * Hardware Limit Switches (Active LOW with internal INPUT_PULLUP)
 * ========================================================================= */
// Bottom Home Limit Switch: Physical home position at column baseline
#define PIN_LIMIT_HOME_BOTTOM   4   // GPIO 4 (Safe GPIO on ESP-WROOM-32 with pullup)

// Top Safety Limit Switch: Prevents carriage from over-traveling at ceiling
#define PIN_LIMIT_SAFETY_TOP    5   // GPIO 5 (Safe GPIO on ESP-WROOM-32 with pullup)

// Optional Built-in status LED
#define PIN_STATUS_LED          2

/* =========================================================================
 * Mechanical & Kinematic Calibration (Calibrated from physical measurement)
 * ========================================================================= */
// Measured calibration: 10,000 steps moved 71.5 cm -> 10000 / 71.5 = 139.86 steps/cm
#define STEPS_PER_CM            139.86f
#define STEPS_PER_MM            (STEPS_PER_CM / 10.0f) // ~13.99 steps/mm

// Direction configuration (adjust based on motor coil wiring)
// true = Clockwise moves carriage UP, false = Clockwise moves carriage DOWN
#define DIR_UP                  true
#define DIR_DOWN                false

// Motion Profile Limits (Configured for steady, smooth, clinical motion)
#define MAX_SPEED_STEPS_SEC     1200.0f  // ~8.6 cm/s steady, controlled travel
#define ACCELERATION_STEPS_SEC2 800.0f   // Smooth, jerk-free acceleration ramp
#define HOMING_SPEED_STEPS_SEC  500.0f   // Gentle homing speed (~3.6 cm/s)

// Physical travel limits
#define HOME_BASELINE_HEIGHT_CM 49.0f    // Lower limit switch physical height (49 cm from floor)
#define MAX_TRAVEL_CM           155.0f   // Maximum stroke travel above baseline (up to 204 cm)

#endif // STEPPER_PINS_H
