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
 * Mechanical & Kinematic Calibration (Calibrated from physical multi-point testing)
 * ========================================================================= */
// Measured calibration: 80.0 cm commanded stroke moved 70.0 cm physical -> 139.86 / 0.875 = 160.0 steps/cm
#define STEPS_PER_CM            160.0f
#define STEPS_PER_MM            (STEPS_PER_CM / 10.0f) // 16.0 steps/mm

// Direction configuration (adjust based on motor coil wiring)
// true = Clockwise moves carriage UP, false = Clockwise moves carriage DOWN
#define DIR_UP                  true
#define DIR_DOWN                false

// Motion Profile Limits (Configured for steady, smooth, clinical motion)
#define MAX_SPEED_STEPS_SEC     1200.0f  // ~7.5 cm/s steady, controlled travel
#define ACCELERATION_STEPS_SEC2 800.0f   // Smooth, jerk-free acceleration ramp
#define HOMING_SPEED_STEPS_SEC  500.0f   // Gentle homing speed (~3.1 cm/s)

// Physical travel limits
#define HOME_BASELINE_HEIGHT_CM 46.0f    // Lower limit switch physical height (46.0 cm from floor to carriage)
#define MAX_TRAVEL_CM           127.0f   // Maximum stroke travel above baseline (46.0 + 127.0 = 173.0 cm top limit)

#endif // STEPPER_PINS_H
