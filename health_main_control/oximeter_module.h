#ifndef OXIMETER_MODULE_H
#define OXIMETER_MODULE_H

#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include "config.h"

extern MAX30105 particleSensor;
extern uint32_t irBuffer[100];
extern uint32_t redBuffer[100];
extern int32_t bufferLength, spo2, heartRate;
extern int8_t validSPO2, validHeartRate;
extern float user_hr, user_sp02; // Changed from double
extern char hrStatus[8], sp02Status[8]; // Changed from String

// Task-based reading
extern volatile bool oximeter_reading_in_progress;
extern volatile bool oximeter_reading_complete;

void initOximeter();
void readOximeter();
void startOximeterTask(); // Non-blocking task start
bool isOximeterReady();   // Check if reading is complete

#endif