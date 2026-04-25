# MEDIC-BOT Control Unit - Modular Medical Monitoring System

## Overview
The MEDIC-BOT is an ESP32-based medical monitoring system that collects vital health data through multiple sensors, provides user authentication, and stores data in Firebase cloud database for remote monitoring.

## Features
- **Multi-sensor Health Monitoring**: Heart rate, SpO2, temperature, height, weight, BMI
- **Dual Authentication**: RFID cards and fingerprint biometrics
- **Wireless Communication**: ESP-NOW protocol for sensor data collection
- **Cloud Integration**: Firebase real-time database storage
- **Status Classification**: Automatic health status categorization
- **Modular Architecture**: Clean, maintainable code structure

## Hardware Requirements

### Main Components
- **ESP32-S3** (Main controller)
- **AS608 Fingerprint Sensor** (Biometric authentication)
- **MFRC522 RFID Reader** (Card authentication)
- **MAX30102 Oximeter** (Heart rate & SpO2)
- **Mode Switch** (Enrollment/Operation mode)
- **Buzzer & LEDs** (Status indicators)

### Pin Configuration
```
SS_PIN = 21          // RFID SPI Slave Select
RST_PIN = 47         // RFID Reset
MODE_SWITCH = 4      // Mode selection switch
MP1 = 1, MP2 = 2     // Motor control pins
BUZZER = 42          // Status buzzer
```

## Software Architecture

### File Structure
```
MEDIC_BOT_CONTROL_MAIN/
├── MEDIC_BOT_CONTROL_MAIN.ino    # Main program + Firebase
├── config.h                      # Configuration constants
├── esp_now_module.h/.cpp         # Wireless communication
├── fingerprint_module.h/.cpp     # Biometric authentication
├── rfid_module.h/.cpp            # RFID card reader
├── oximeter_module.h/.cpp        # Health monitoring
└── README.md                     # This file
```

### Module Descriptions

#### 1. **config.h** - Configuration Module
- Centralized constants and pin definitions
- WiFi credentials and Firebase API keys
- System parameters (HEIGHT_REF = 2.20m)

#### 2. **esp_now_module** - Wireless Communication
- **Purpose**: Receives sensor data from remote ESP32 boards
- **Key Functions**:
  - `initESPNow()`: Initialize wireless protocol
  - `OnDataRecv()`: Process incoming sensor data
  - `readESPNowData()`: Calculate BMI and health status
- **Data Received**: Weight, temperature, height measurements

#### 3. **fingerprint_module** - Biometric Authentication
- **Purpose**: User identification via fingerprint scanning
- **Key Functions**:
  - `initFingerprint()`: Setup AS608 sensor
  - `enrollFingerprint()`: Register new users
  - `readFingerprint()`: Authenticate users
- **Output**: User ID stored in `fidString`

#### 4. **rfid_module** - Card Authentication
- **Purpose**: Alternative user identification via RFID cards
- **Key Functions**:
  - `initRFID()`: Setup MFRC522 reader
  - `readRFID()`: Read card UID
- **Output**: Card ID stored in `tidString`

#### 5. **oximeter_module** - Health Monitoring
- **Purpose**: Measure vital signs using MAX30102 sensor
- **Key Functions**:
  - `initOximeter()`: Configure sensor parameters
  - `readOximeter()`: Measure heart rate and SpO2
- **Output**: Health data with status classification

#### 6. **Firebase Integration** (Main File)
- **Purpose**: Cloud database connectivity
- **Key Functions**:
  - `initFirebase()`: Connect to cloud database
  - `writeFirebaseDB()`: Upload all sensor data
- **Database Structure**: `MBOT-READINGS/[SENSOR]/VALUE` and `STATUS`

## Operation Modes

### Enrollment Mode (MODE_SWITCH = HIGH)
1. System prompts for fingerprint ID (1-127)
2. User places finger twice for template creation
3. Fingerprint stored in sensor memory
4. Ready for authentication

### Operation Mode (MODE_SWITCH = LOW)
1. **Authentication Phase**:
   - Read RFID card → `tidString`
   - Scan fingerprint → `fidString`
2. **Health Monitoring Phase**:
   - Measure heart rate and SpO2
   - Receive weight, height, temperature via ESP-NOW
   - Calculate BMI automatically
3. **Data Upload Phase**:
   - Upload all readings to Firebase
   - Include health status classifications

## Health Status Classifications

### Heart Rate (BPM)
- **SLOW**: < 60
- **NORM**: 60-100
- **FAST**: 100-160
- **EXTR**: > 160

### SpO2 (%)
- **NORM**: > 95
- **MILD**: 90-95
- **MHYP**: 85-90
- **SHYP**: < 85

### BMI Categories
- **UNDER**: < 18.5
- **NORM**: 18.5-24.9
- **OVER**: 25-29.9
- **OBES1**: 30-34.9
- **OBES2**: 35-39.9
- **OBES3**: > 40

### Temperature Status
- **LOW**: < 32°C
- **NORM**: 32-37.5°C
- **HIGH**: 37.5-40°C
- **HHYP**: > 40°C

## Setup Instructions

### 1. Hardware Connections
- Connect sensors according to pin configuration
- Ensure proper power supply (5V for sensors)
- Install mode switch for operation control

### 2. Software Configuration
- Update WiFi credentials in `config.h`
- Configure Firebase API key and database URL
- Install required Arduino libraries:
  - Firebase ESP Client
  - Adafruit Fingerprint
  - MFRC522
  - MAX30105

### 3. Library Dependencies
```cpp
#include <esp_now.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Adafruit_Fingerprint.h>
#include <MFRC522.h>
#include <MAX30105.h>
#include <spo2_algorithm.h>
```

### 4. Compilation
- Open `MEDIC_BOT_CONTROL_MAIN.ino` in Arduino IDE
- Select ESP32-S3 board
- All `.cpp` files compile automatically
- Upload to ESP32

## Firebase Database Structure
```
MBOT-READINGS/
├── MBOT-HLASER/
│   ├── VALUE: "1.75"
│   └── STATUS: "AVG"
├── MBOT-WEIGHT/
│   ├── VALUE: "70.5"
│   └── STATUS: "NORM"
├── MBOT-HR/
│   ├── VALUE: "75"
│   └── STATUS: "NORM"
└── [Additional sensors...]
```

## Usage Example
1. **Power on** → System initializes all modules
2. **Set mode switch LOW** → Operation mode
3. **Present RFID card** → Card ID captured
4. **Place finger on sensor** → Fingerprint authenticated
5. **Wait for measurements** → Health data collected
6. **Data automatically uploaded** → Available in Firebase

## Troubleshooting

### Common Issues
- **Fingerprint not recognized**: Re-enroll or clean sensor
- **RFID not reading**: Check card proximity and connections
- **Firebase connection failed**: Verify WiFi credentials and API key
- **Sensor initialization failed**: Check wiring and power supply

### Serial Monitor Output
- Monitor at 115200 baud rate
- Shows initialization status for each module
- Displays sensor readings and Firebase upload status

## Development Notes
- Modular design allows easy sensor addition/removal
- Global variables shared between modules
- Firebase integration prevents multiple definition errors
- Status mapping provides meaningful health insights

## License
iDEPP PROJECTS 2024 - MEDIC-BOT Medical Monitoring System