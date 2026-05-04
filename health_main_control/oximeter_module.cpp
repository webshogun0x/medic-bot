#include "oximeter_module.h"

MAX30105 particleSensor;
uint32_t irBuffer[100];
uint32_t redBuffer[100];
int32_t bufferLength, spo2, heartRate;
int8_t validSPO2, validHeartRate;
float user_hr = 80, user_sp02 = 98;
char hrStatus[8] = "NORM", sp02Status[8] = "NORM";

// Task control variables
volatile bool oximeter_reading_in_progress = false;
volatile bool oximeter_reading_complete = false;
TaskHandle_t oximeterTaskHandle = NULL;

void initOximeter() {
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println(F("MAX30105 was not found. Please check wiring/power."));
    while (1);
  }
  
  byte ledBrightness = 60;
  byte sampleAverage = 4;
  byte ledMode = 2;
  byte sampleRate = 100;
  int pulseWidth = 411;
  int adcRange = 4096;
  
  particleSensor.enableDIETEMPRDY();
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
}

void oximeterReadTask(void *parameter) {
  oximeter_reading_in_progress = true;
  oximeter_reading_complete = false;
  
  Serial.println("[Oximeter Task] Starting reading...");
  
  float temperature = 0;
  float temperatureF = 0;
  long irThreshold = 15000;
  long irValue = particleSensor.getIR();

  if (irValue > irThreshold) {
    temperature = particleSensor.readTemperature();
    temperatureF = particleSensor.readTemperatureF();

    bufferLength = 100;
    for (byte i = 0; i < bufferLength; i++) {
      while (particleSensor.available() == false)
        particleSensor.check();

      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample();
      
      // Allow other tasks to run
      if (i % 10 == 0) {
        vTaskDelay(1);
      }
    }

    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);
    
    for (byte i = 25; i < 100; i++) {
      redBuffer[i - 25] = redBuffer[i];
      irBuffer[i - 25] = irBuffer[i];
    }

    for (byte i = 75; i < 100; i++) {
      while (particleSensor.available() == false)
        particleSensor.check();

      // READ_LED removed - no longer needed
      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample();
      
      // Allow other tasks to run
      if (i % 10 == 0) {
        vTaskDelay(1);
      }
    }

    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);
    
    user_hr = heartRate;
    user_sp02 = spo2;

    // HR Status mapping
    if (!isnan(user_hr)) {
      if (user_hr < 60) strcpy(hrStatus, "SLOW");
      else if (user_hr < 100) strcpy(hrStatus, "NORM");
      else if (user_hr < 160) strcpy(hrStatus, "FAST");
      else strcpy(hrStatus, "EXTR");
    } else strcpy(hrStatus, "NIL");

    // SPO2 Status mapping
    if (!isnan(user_sp02)) {
      if (user_sp02 > 95) strcpy(sp02Status, "NORM");
      else if (user_sp02 > 90) strcpy(sp02Status, "MILD");
      else if (user_sp02 > 85) strcpy(sp02Status, "MHYP");
      else strcpy(sp02Status, "SHYP");
    } else strcpy(sp02Status, "NIL");
    
    Serial.println("[Oximeter Task] Reading complete");
    Serial.print("HR: "); Serial.print(user_hr); Serial.print(" bpm, SpO2: "); Serial.print(user_sp02); Serial.println("%");
  } else {
    Serial.println("[Oximeter Task] No finger detected");
  }
  
  oximeter_reading_in_progress = false;
  oximeter_reading_complete = true;
  oximeterTaskHandle = NULL;
  vTaskDelete(NULL);
}

void startOximeterTask() {
  if (oximeter_reading_in_progress) {
    Serial.println("[Oximeter] Reading already in progress");
    return;
  }
  
  oximeter_reading_complete = false;
  xTaskCreate(oximeterReadTask, "oximeter_task", 4096, NULL, 1, &oximeterTaskHandle);
}

bool isOximeterReady() {
  return oximeter_reading_complete && !oximeter_reading_in_progress;
}

void readOximeter() {
  // Legacy blocking function - now uses task internally
  startOximeterTask();
  
  // Wait for completion (blocking for backward compatibility)
  while (!isOximeterReady()) {
    delay(100);
  }
}