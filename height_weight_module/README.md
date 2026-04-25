# Height and Weight Measurement Module

ESP32-S3 firmware for measuring height and weight using load cell, stepper motor, and ultrasonic sensor.

## Hardware Requirements

1. **ESP32-S3 Development Board**
2. **HX711 Load Cell Amplifier** - for weight measurement
3. **TB6600 Stepper Motor Driver** - controls NEMA 24 stepper motor
4. **NEMA 24 Stepper Motor** - vertical movement
5. **HC-SR04 Ultrasonic Sensor** - height measurement
6. **Load Cell** - weight sensor

## Pin Connections

### HX711 Load Cell Amplifier
- DOUT → GPIO 4
- SCK → GPIO 5
- VCC → 5V
- GND → GND

### TB6600 Stepper Driver
- PUL+ → GPIO 6 (Pulse)
- DIR+ → GPIO 7 (Direction)
- ENA+ → GPIO 8 (Enable)
- PUL-, DIR-, ENA- → GND
- Motor connections: A+, A-, B+, B-
- Power: 12-24V DC

### HC-SR04 Ultrasonic Sensor
- TRIG → GPIO 9
- ECHO → GPIO 10
- VCC → 5V
- GND → GND

## Configuration

### Motor Settings (Adjust in code)
```cpp
#define STEPS_PER_REV       200     // Steps per revolution
#define MICROSTEPS          8       // TB6600 microstepping setting
#define LEAD_SCREW_PITCH_MM 8.0     // Lead screw pitch in mm
#define CLOCKWISE_IS_UP     true    // Motor direction
```

### System Constants
```cpp
#define HOME_POSITION_CM        150.0   // Home position height
#define SENSOR_HEIGHT_CM        195.0   // Ultrasonic sensor height
#define WEIGHT_THRESHOLD_KG     40.0    // Person detection threshold
```

### HX711 Calibration
1. Upload code with default calibration factor
2. Place known weight on scale
3. Adjust `HX711_CALIBRATION_FACTOR` until reading is correct
4. Re-upload code

### ESP-NOW Setup
Replace MAC address in code:
```cpp
uint8_t mainControllerMAC[] = {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX};
```

## Arduino IDE Setup

### Required Libraries
Install via Arduino Library Manager:
1. **HX711 Arduino Library** by Bogdan Necula
2. **ESP32** board support (already installed)

### Board Settings
- Board: "ESP32S3 Dev Module"
- USB CDC On Boot: "Enabled"
- Upload Speed: 921600
- Port: Select your ESP32-S3 port

## Operation Logic

1. **Startup**: Motor moves to home position (150cm)
2. **Wait**: System waits for weight ≥ 40kg
3. **Detect Person**: Load cell detects person
4. **Measure Height**: 
   - Ultrasonic reads distance to head
   - Height = 195cm - distance
5. **Adjust Motor**: 
   - Calculate: person_height - 150cm
   - Move motor up/down accordingly
6. **Calculate BMI**: BMI = weight / (height_m²)
7. **Send Data**: Transmit via ESP-NOW to main controller
8. **Reset**: Wait for person to step off, return to home

## Troubleshooting

### Motor moves wrong direction
Change in code:
```cpp
#define CLOCKWISE_IS_UP false  // Reverse direction
```

### Weight readings incorrect
1. Tare the scale (remove all weight, reset ESP32)
2. Adjust calibration factor
3. Check HX711 wiring

### Ultrasonic not reading
1. Check 5V power supply
2. Verify TRIG/ECHO pins
3. Ensure clear line of sight

### ESP-NOW not connecting
1. Get MAC address from Serial Monitor
2. Update main controller with this MAC
3. Update this code with main controller MAC

## Serial Monitor Output

```
=== HEIGHT & WEIGHT MODULE STARTING ===
Initializing HX711...
HX711 initialized and tared.
Initializing stepper motor...
Stepper motor initialized.
Initializing ultrasonic sensor...
Ultrasonic sensor initialized.
Initializing ESP-NOW...
This device MAC: XX:XX:XX:XX:XX:XX
ESP-NOW initialized.
Moving to home position...
At home position (150cm)
System ready. Waiting for person...
```

## Data Structure

Data sent to main controller:
```cpp
typedef struct {
  float weight_kg;      // Weight in kg
  float height_cm;      // Height in cm
  float bmi;            // Calculated BMI
  uint32_t timestamp;   // Measurement time
} measurement_data_t;
```
