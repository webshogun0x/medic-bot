#ifndef HEIGHT_PINS_H
#define HEIGHT_PINS_H

#include <stdint.h>

/* =========================================================================
 * I2C Interface (VL53L0X Laser Distance / ToF Sonar)
 * ========================================================================= */
#define PIN_I2C_SDA             21
#define PIN_I2C_SCL             22

/* =========================================================================
 * Patient Height Ultrasonic Array (3-Point Spatial Triangulation)
 * ========================================================================= */
// Sonar 1 (Left shoulder / head)
#define PIN_SONAR1_TRIG         12
#define PIN_SONAR1_ECHO         13

// Sonar 2 (Center crown / vertex)
#define PIN_SONAR2_TRIG         14
#define PIN_SONAR2_ECHO         27

// Sonar 3 (Right shoulder / head)
#define PIN_SONAR3_TRIG         26
#define PIN_SONAR3_ECHO         25

/* =========================================================================
 * Gantry Carriage Position Tracker
 * ========================================================================= */
// Sonar 4 (Tracks vertical gantry / forehead carriage height)
#define PIN_GANTRY_TRIG         33
#define PIN_GANTRY_ECHO         32

/* =========================================================================
 * System Geometry & Calibration Constants
 * ========================================================================= */
// Default overhead sensor mount height above the weight platform (in cm)
#define DEFAULT_STAND_HEIGHT_CM 220.0f

// Sensor physical limits (in cm)
#define MIN_MEASURABLE_DIST_CM  3.0f
#define MAX_MEASURABLE_DIST_CM  250.0f

// Realistic human height boundaries for clinical validation (in cm)
#define MIN_VALID_PATIENT_HT_CM 60.0f
#define MAX_VALID_PATIENT_HT_CM 220.0f

// Inter-sensor delay to prevent acoustic reflection cross-talk (in ms)
#define ACOUSTIC_STAGGER_MS     15

#endif // HEIGHT_PINS_H
