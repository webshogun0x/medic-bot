/*
  HEIGHT AND WEIGHT MEASUREMENT MODULE
  ESP32-S3 with HX711, TB6600 Stepper Driver, and Ultrasonic Sensor

  Hardware:
  - HX711 Load Cell Amplifier
  - TB6600 Stepper Motor Driver (NEMA 24)
  - HC-SR04 Ultrasonic Sensor
  - ESP-NOW communication with main controller

Author: Idowu Oluwatimileyin (The_Shogunate)
Date: November 26, 2025
*/

#include <HX711.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>

// ===== PIN DEFINITIONS =====
// HX711 Load Cell
#define HX711_DOUT_PIN  4
#define HX711_SCK_PIN   5

// TB6600 Stepper Driver
#define STEPPER_PUL_PIN 6   // Pulse pin
#define STEPPER_DIR_PIN 7   // Direction pin
#define STEPPER_ENA_PIN 8   // Enable pin (LOW = enabled)

// HC-SR04 Ultrasonic Sensor
#define ULTRASONIC_TRIG_PIN 9
#define ULTRASONIC_ECHO_PIN 10

// Limit Switch for Homing
#define LIMIT_SWITCH_PIN 11  // Bottom limit switch (home position)

// LiDAR I2C Pins (ESP32-S3)
#define I2C_SDA 1
#define I2C_SCL 2

// ===== MOTOR CONFIGURATION =====
// NEMA 24 typical: 200 steps/rev, with 8mm lead screw
#define STEPS_PER_REV       200
#define MICROSTEPS          8      // TB6600 microstepping (adjust based on DIP switches)
#define LEAD_SCREW_PITCH_MM 8.0    // mm per revolution
#define STEPS_PER_MM        ((STEPS_PER_REV * MICROSTEPS) / LEAD_SCREW_PITCH_MM)

// Motor direction: true = clockwise (UP), false = counter-clockwise (DOWN)
// ADJUST THIS IF MOTOR MOVES IN WRONG DIRECTION
#define CLOCKWISE_IS_UP     true

// ===== SYSTEM CONSTANTS =====
#define HOME_POSITION_CM        150.0   // Home position height in cm
#define SENSOR_HEIGHT_CM        195.0   // Sensor mounted height
#define WEIGHT_THRESHOLD_KG     50.0    // Minimum weight to detect person (increased from 40)
#define MOTOR_SPEED_DELAY_US    800     // Delay between steps (lower = faster)
#define MAX_TRAVEL_CM           50.0    // Maximum travel distance from home
#define HOMING_SPEED_DELAY_US   1200    // Slower speed for homing

// ===== CALIBRATION =====
// HX711 calibration factor (adjust after calibration)
#define HX711_CALIBRATION_FACTOR -7050.0

// ===== ESP-NOW CONFIGURATION =====
// MAC Address of main controller (MEDIC_BOT_CONTROL_MAIN)
uint8_t mainControllerMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ===== GLOBAL OBJECTS =====
HX711 scale;
Adafruit_VL53L0X lox = Adafruit_VL53L0X();

// Position tracking
float current_position_cm = HOME_POSITION_CM;  // Track current position
bool is_homed = false;  // Track if system has been homed

// ===== DATA STRUCTURE =====
typedef struct {
  float weight_kg;
  float height_sonar_cm;
  float height_lidar_cm;
  float bmi_sonar;
  float bmi_lidar;
  uint32_t timestamp;
} measurement_data_t;

measurement_data_t current_measurement = {0, 0, 0, 0, 0, 0};

// ===== FUNCTION PROTOTYPES =====
void initHX711();
void initStepper();
void initUltrasonic();
void initLidar();
void initESPNow();
void moveToHome();
void moveStepper(float distance_cm, bool moveUp);
float readWeight();
float readHeightSonar();
float readHeightLidar();
bool detectPerson();
void takeMeasurement();
void sendDataToMain();
void onDataRequest(const uint8_t *mac, const uint8_t *data, int len);
void onDataSent(const uint8_t *mac, esp_now_send_status_t status);

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(1000);
  Serial.println("=== HEIGHT & WEIGHT MODULE STARTING ===");
  
  // Initialize hardware
  initHX711();
  initStepper();
  initUltrasonic();
  initLidar();
  initESPNow();
  
  // Move to home position
  Serial.println("Moving to home position...");
  moveToHome();
  
  Serial.println("System ready. Waiting for person...");
}

// ===== MAIN LOOP =====
void loop() {
  // Check if person is on the scale
  if (detectPerson()) {
    Serial.println("Person detected!");
    delay(4000); // Wait for person to stabilize
    
    // Take measurement
    takeMeasurement();
    
    // Wait for person to step off
    Serial.println("Measurement complete. Please step off...");
    while (detectPerson()) {
      delay(2000);
    }
    
    // Return to home position
    Serial.println("Returning to home position...");
    moveToHome();
    Serial.println("Ready for next measurement.");
  }
  
  delay(100);
}

// ===== HARDWARE INITIALIZATION =====
void initHX711() {
  Serial.println("Initializing HX711...");
  scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
  scale.set_scale(HX711_CALIBRATION_FACTOR);
  scale.tare(); // Reset scale to 0
  Serial.println("HX711 initialized and tared.");
}

void initStepper() {
  Serial.println("Initializing stepper motor...");
  pinMode(STEPPER_PUL_PIN, OUTPUT);
  pinMode(STEPPER_DIR_PIN, OUTPUT);
  pinMode(STEPPER_ENA_PIN, OUTPUT);
  pinMode(LIMIT_SWITCH_PIN, INPUT_PULLUP);  // Limit switch with pullup
  
  digitalWrite(STEPPER_ENA_PIN, LOW);  // Enable motor
  digitalWrite(STEPPER_PUL_PIN, LOW);
  digitalWrite(STEPPER_DIR_PIN, LOW);
  
  Serial.println("Stepper motor initialized.");
}

void initUltrasonic() {
  Serial.println("Initializing ultrasonic sensor...");
  pinMode(ULTRASONIC_TRIG_PIN, OUTPUT);
  pinMode(ULTRASONIC_ECHO_PIN, INPUT);
  digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
  Serial.println("Ultrasonic sensor initialized.");
}

void initLidar() {
  Serial.println("Initializing VL53L0X LiDAR...");
  if (!lox.begin()) {
    Serial.println(F("Failed to boot VL53L0X"));
  } else {
    Serial.println(F("VL53L0X LiDAR initialized."));
  }
}

void initESPNow() {
  Serial.println("Initializing ESP-NOW...");
  WiFi.mode(WIFI_STA);
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register callbacks
  esp_now_register_recv_cb(onDataRequest);
  esp_now_register_send_cb(onDataSent);
  
  // Add main controller as peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mainControllerMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
  
  Serial.println("ESP-NOW initialized.");
  Serial.print("This device MAC: ");
  Serial.println(WiFi.macAddress());
}

// ===== MOTOR CONTROL =====
void moveToHome() {
  Serial.println("Homing stepper motor...");
  
  // Check if already at home
  if (digitalRead(LIMIT_SWITCH_PIN) == LOW) {
    Serial.println("Already at home position");
    current_position_cm = HOME_POSITION_CM;
    is_homed = true;
    return;
  }
  
  // Move down until limit switch is triggered
  digitalWrite(STEPPER_DIR_PIN, CLOCKWISE_IS_UP ? LOW : HIGH);  // Move down
  
  int steps_taken = 0;
  int max_homing_steps = (int)(MAX_TRAVEL_CM * 10 * STEPS_PER_MM * 2);  // Safety limit
  
  while (digitalRead(LIMIT_SWITCH_PIN) == HIGH && steps_taken < max_homing_steps) {
    digitalWrite(STEPPER_PUL_PIN, HIGH);
    delayMicroseconds(HOMING_SPEED_DELAY_US);
    digitalWrite(STEPPER_PUL_PIN, LOW);
    delayMicroseconds(HOMING_SPEED_DELAY_US);
    steps_taken++;
    
    // Check limit switch every 100 steps
    if (steps_taken % 100 == 0) {
      delay(1);
    }
  }
  
  if (digitalRead(LIMIT_SWITCH_PIN) == LOW) {
    Serial.println("Home position reached");
    current_position_cm = HOME_POSITION_CM;
    is_homed = true;
  } else {
    Serial.println("WARNING: Homing failed - limit switch not triggered!");
    is_homed = false;
  }
  
  delay(500);
}

void moveStepper(float distance_cm, bool moveUp) {
  if (distance_cm <= 0) return;
  
  // Safety check: don't move if not homed
  if (!is_homed) {
    Serial.println("ERROR: System not homed! Call moveToHome() first.");
    return;
  }
  
  // Safety check: prevent moving beyond limits
  float target_position = current_position_cm + (moveUp ? distance_cm : -distance_cm);
  if (target_position < HOME_POSITION_CM || target_position > (HOME_POSITION_CM + MAX_TRAVEL_CM)) {
    Serial.print("ERROR: Target position ");
    Serial.print(target_position);
    Serial.println(" cm is out of safe range!");
    return;
  }
  
  long steps = (long)(distance_cm * 10 * STEPS_PER_MM); // Convert cm to mm
  
  // Set direction
  bool directionPin = (moveUp == CLOCKWISE_IS_UP);
  digitalWrite(STEPPER_DIR_PIN, directionPin ? HIGH : LOW);
  
  Serial.print("Moving ");
  Serial.print(moveUp ? "UP" : "DOWN");
  Serial.print(" ");
  Serial.print(distance_cm);
  Serial.print("cm (");
  Serial.print(steps);
  Serial.println(" steps)");
  
  // Move motor with limit switch safety check
  for (long i = 0; i < steps; i++) {
    // Emergency stop if limit switch triggered while moving down
    if (!moveUp && digitalRead(LIMIT_SWITCH_PIN) == LOW) {
      Serial.println("EMERGENCY STOP: Limit switch triggered!");
      current_position_cm = HOME_POSITION_CM;
      break;
    }
    
    digitalWrite(STEPPER_PUL_PIN, HIGH);
    delayMicroseconds(MOTOR_SPEED_DELAY_US);
    digitalWrite(STEPPER_PUL_PIN, LOW);
    delayMicroseconds(MOTOR_SPEED_DELAY_US);
  }
  
  // Update position tracking
  current_position_cm = target_position;
  
  Serial.print("Current position: ");
  Serial.print(current_position_cm);
  Serial.println(" cm");
  
  delay(500); // Settle time
}

// ===== SENSOR READING =====
float readWeight() {
  if (scale.is_ready()) {
    float weight = scale.get_units(10); // Average of 10 readings
    Serial.print("Weight: ");
    Serial.print(weight);
    Serial.println(" kg");
    return weight;
  }
  return 0.0;
}

float readHeightSonar() {
  // Take multiple readings and average
  float total = 0;
  int validReadings = 0;
  
  for (int i = 0; i < 5; i++) {
    digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(ULTRASONIC_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
    
    long duration = pulseIn(ULTRASONIC_ECHO_PIN, HIGH, 30000); // 30ms timeout
    
    if (duration > 0) {
      float distance_cm = duration * 0.034 / 2.0; // Speed of sound
      if (distance_cm > 0 && distance_cm < 200) { // Valid range
        total += distance_cm;
        validReadings++;
      }
    }
    delay(50);
  }
  
  if (validReadings > 0) {
    float avg_distance = total / validReadings;
    float person_height = SENSOR_HEIGHT_CM - avg_distance;
    
    Serial.print("Sonar distance: ");
    Serial.print(avg_distance);
    Serial.print(" cm, Person height: ");
    Serial.print(person_height);
    Serial.println(" cm");
    
    return person_height;
  }
  
  return 0.0;
}

float readHeightLidar() {
  VL53L0X_RangingMeasurementData_t measure;
  float total = 0;
  int validReadings = 0;
  
  for (int i = 0; i < 5; i++) {
    lox.rangingTest(&measure, false);
    if (measure.RangeStatus != 4) { // Status 4 is out of range
      float distance_cm = measure.PointToPointRange / 10.0; // mm to cm
      total += distance_cm;
      validReadings++;
    }
    delay(50);
  }
  
  if (validReadings > 0) {
    float avg_distance = total / validReadings;
    float person_height = SENSOR_HEIGHT_CM - avg_distance;
    
    Serial.print("LiDAR distance: ");
    Serial.print(avg_distance);
    Serial.print(" cm, Person height: ");
    Serial.print(person_height);
    Serial.println(" cm");
    
    return person_height;
  }
  
  return 0.0;
}

bool detectPerson() {
  float weight = readWeight();
  return (weight >= WEIGHT_THRESHOLD_KG);
}

// ===== MEASUREMENT =====
void takeMeasurement() {
  Serial.println("=== TAKING MEASUREMENT ===");
  
  // Read weight
  current_measurement.weight_kg = readWeight();
  
  // Read heights
  current_measurement.height_sonar_cm = readHeightSonar();
  current_measurement.height_lidar_cm = readHeightLidar();
  
  // Use Sonar as primary for motor movement (or whichever is valid)
  float primary_height = (current_measurement.height_sonar_cm > 0) ? current_measurement.height_sonar_cm : current_measurement.height_lidar_cm;
  
  // Calculate how much to move motor
  float distance_to_move = primary_height - HOME_POSITION_CM;
  
  if (distance_to_move > 0) {
    // Person is taller than home position, move up
    moveStepper(distance_to_move, true);
  } else if (distance_to_move < 0) {
    // Person is shorter than home position, move down
    moveStepper(-distance_to_move, false);
  }
  
  // Calculate BMIs
  if (current_measurement.height_sonar_cm > 0) {
    float height_m = current_measurement.height_sonar_cm / 100.0;
    current_measurement.bmi_sonar = current_measurement.weight_kg / (height_m * height_m);
  }
  
  if (current_measurement.height_lidar_cm > 0) {
    float height_m = current_measurement.height_lidar_cm / 100.0;
    current_measurement.bmi_lidar = current_measurement.weight_kg / (height_m * height_m);
  }
  
  current_measurement.timestamp = millis();
  
  Serial.println("=== MEASUREMENT RESULTS ===");
  Serial.print("Weight: "); Serial.print(current_measurement.weight_kg); Serial.println(" kg");
  Serial.print("Sonar Height: "); Serial.print(current_measurement.height_sonar_cm); Serial.println(" cm");
  Serial.print("LiDAR Height: "); Serial.print(current_measurement.height_lidar_cm); Serial.println(" cm");
  Serial.print("BMI (Sonar): "); Serial.println(current_measurement.bmi_sonar);
  Serial.print("BMI (LiDAR): "); Serial.println(current_measurement.bmi_lidar);
  Serial.println("==========================");
  
  // Send data to main controller
  sendDataToMain();
}

// ===== ESP-NOW COMMUNICATION =====
void sendDataToMain() {
  Serial.println("Sending data to main controller...");
  
  esp_err_t result = esp_now_send(mainControllerMAC, 
                                   (uint8_t *)&current_measurement, 
                                   sizeof(measurement_data_t));
  
  if (result == ESP_OK) {
    Serial.println("Data sent successfully");
  } else {
    Serial.println("Error sending data");
  }
}

void onDataRequest(const uint8_t *mac, const uint8_t *data, int len) {
  Serial.println("Data request received from main controller");
  
  // Check if it's a request for data
  if (len > 0 && data[0] == 0xAA) { // 0xAA = request code
    sendDataToMain();
  }
}

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  Serial.print("Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}
