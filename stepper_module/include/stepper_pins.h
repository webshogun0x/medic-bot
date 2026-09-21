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
 * Mechanical & Kinematic Parameters (NEMA Stepper + Lead Screw)
 * ========================================================================= */
#define MOTOR_STEPS_PER_REV     200     // 1.8 deg step angle
#define MICROSTEPS              8       // TB6600 DIP switch setting (1/8 microstepping)
#define LEAD_SCREW_PITCH_MM     8.0f    // 8 mm travel per full shaft revolution
#define STEPS_PER_MM            ((float)(MOTOR_STEPS_PER_REV * MICROSTEPS) / LEAD_SCREW_PITCH_MM) // 200 steps/mm
#define STEPS_PER_CM            (STEPS_PER_MM * 10.0f) // 2000 steps/cm

// Direction configuration (adjust based on motor coil wiring)
// true = Clockwise moves carriage UP, false = Clockwise moves carriage DOWN
#define DIR_UP                  true
#define DIR_DOWN                false

// Motion Profile Limits
#define MAX_SPEED_STEPS_SEC     4000.0f  // ~20 mm/s max speed
#define ACCELERATION_STEPS_SEC2 2000.0f  // Smooth jerk-free ramp
#define HOMING_SPEED_STEPS_SEC  1500.0f  // Gentle speed during switch homing

// Physical travel limits
#define HOME_BASELINE_HEIGHT_CM 140.0f   // Gantry baseline resting height
#define MAX_TRAVEL_CM           65.0f    // Maximum stroke travel above baseline (up to 205 cm)

#endif // STEPPER_PINS_H
