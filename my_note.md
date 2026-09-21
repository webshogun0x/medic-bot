  ### Step 2: Download / Backup the Firmware

  Run the following command to read the entire 16 MB (0x1000000 bytes) flash memory starting at address 0x0:

    ~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
      --chip esp32s3 \
      --port /dev/ttyUSB0 \
      --baud 921600 \
      read_flash 0x0 0x1000000 sunton_screen_backup_16MB.bin

  │ Tip
  │ At 921600 baud, dumping the complete 16MB flash image takes approximately 25–35 seconds.
  ──────
  ### Step 3: How to Restore / Re-Upload If Needed

  If you ever need to restore the screen to this exact backup, run the write_flash command:

    ~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
      --chip esp32s3 \
      --port /dev/ttyUSB0 \
      --baud 921600 \
      write_flash 0x0 sunton_screen_backup_16MB.bin

  ### What Does the Backup Include?

  Because this dumps from address 0x0 through the entire 16MB space, the resulting file (sunton_screen_backup_16MB.bin) contains:

  1. Bootloader (starting at 0x0)
  2. Partition Table (starting at 0x8000 / 0x9000)
  3. Application Firmware (starting at 0x10000 / 0x20000)
  4. NVS calibration data and assets

 ### The End-to-End System Architecture
                        ┌─────────────────────────────────────────────────────────┐
                        │                   MEDIBOT CLOUD (FIREBASE)               │
                        └───────────▲─────────────────────────────────▲───────────┘
                                    │                                 │
                       HTTPS REST   │                    HTTPS REST   │
                                    │                                 │
               ┌────────────────────┴────────┐              ┌─────────┴──────────────────┐
               │     MEDIC-BOT WEB APP       │              │      MAIN CONTROLLER       │
               │  • Admin issues RFID & ID   │              │     (ESP32-S3 USB-OTG)     │
               │  • Patient registers        │              │  • Central Orchestrator    │
               │  • "Pending Biometric" banner│             │  • Local SQLite on SD Card │
               │  • Longitudinal Vitals Graph│              └──────────┬─────────────────┘
               └─────────────────────────────┘                         │
                                                                       │ High-Speed UART
                                                                       ▼
                                                            ┌────────────────────┐
                                                            │ SUNTON 7" DISPLAY  │
                                                            │  • LVGL 9 UI       │
                                                            │  • Touch Keyboard  │
                                                            │  • BP Manual Entry │
                                                            └────────────────────┘
  ──────
  ### Phase 1: Web App Pre-Registration
  1. Clinic Admin: Assigns an RFID card to a new patient, noting the physical card UID and generating a unique Medical ID (e.g., MB-94021).
  2. Patient Sign-up on Web:
      • Patient fills out: Name, Phone, RFID UID, Email, Date of Birth, Password, Medical ID.
      • Backend creates the patient record in Firebase with:
        {
          "medical_id": "MB-94021",
          "rfid_uid": "4A3F129B",
          "biometric_enrolled": false,
          "fingerprint_slot": null,
          "readings": []
        }

  3. Web Dashboard Initial State:
      • Because biometric_enrolled == false, the web dashboard shows no readings, displaying an amber banner:
      │ ⚠️ Biometric Authorization Required: Please visit the MediBot Kiosk to scan your badge and register your fingerprint to activate your
      │ account.


  ──────
  ### Phase 2: First Kiosk Visit (Biometric Onboarding)

  1. Welcome Screen:
      • Patient selects "New User / Biometric Registration".
  2. RFID Card Scan:
      • Patient taps their RFID card on the MFRC522 reader.
      • Main Controller reads the card UID (e.g., 4A3F129B), displays it on the 7" screen, and queries the database (first checking cloud
      Firebase via Wi-Fi; if offline, checking local SD SQLite).
      • Backend returns the patient’s pre-registered data: Name, Age, Gender, Medical ID.
  3. Fingerprint Enrollment:
      • 7" Screen displays: "Welcome, Sarah Jenkins. Place your finger on the optical sensor."
      • Voice prompt: "Place finger on sensor."
      • Main Controller captures Scan 1 on the AS608, prompts to lift finger, then captures Scan 2.
      • AS608 combines scans into a template model and saves it to a free flash memory slot (e.g., Slot #14).
  4. Account Activation:
      • Main Controller updates the patient’s record with fingerprint_slot: 14 and biometric_enrolled: true.
      • The update is written immediately to local SQLite on the SD card and pushed to Firebase.
      • The screen transitions straight to the Interactive Vitals Dashboard.

  ──────
  ### Phase 3: The Measurement Orchestration (The Heart of the System)

  Here is my full engineering proposal for how the Main Controller orchestrates the distributed microcontrollers over ESP-NOW when the patient
  taps "Take Measurement":

                             ORCHESTRATION SEQUENCE DIAGRAM
                             
     [SUNTON 7"]       [MAIN CONTROLLER]       [SCALE MCU]       [HEIGHT MCU]      [STEPPER MCU]      [TEMP MCU]
          │                    │                    │                 │                  │                │
          │──"READ_OXIMETER"──►│                    │                 │                  │                │
          │   (UART Command)   │                    │                 │                  │                │
          │                    │─── CMD_WEIGHT ────►│                 │                  │                │
          │                    │◄── Weight (68.5kg)─│                 │                  │                │
          │                    │                    │                 │                  │                │
          │                    │─── CMD_HEIGHT ──────────────────────►│                  │                │
          │                    │◄── Height (175cm) ───────────────────│                  │                │
          │                    │                                      │                  │                │
          │                    │─── CMD_MOVE_TO(175cm) ─────────────────────────────────►│                │
          │                    │    (Stepper drives carriage up column to forehead)      │                │
          │                    │◄── CARRIAGE_AT_FOREHEAD_ACK ────────────────────────────│                │
          │                    │                                                         │                │
          │                    │─── CMD_MEASURE_TEMP ────────────────────────────────────────────────────►│
          │                    │    (Carriage IR sensor reads temp; lights up local TM1637: [36.7C])      │
          │                    │◄── Temp (36.7°C) ────────────────────────────────────────────────────────│
          │                    │                                                         │                │
          │                    │─── CMD_RETURN_HOME ────────────────────────────────────►│                │
          │                    │    (Carriage returns to home position)                  │                │
          │                    │                                                                          │
          │                    │ [Parallel Console Task: Reads MAX30102 Oximeter (74 BPM, 98.5% SpO2)]    │
          │                    │ [Computes BMI = 68.5 / (1.75)² = 22.4]                                   │
          │                    │                                                                          │
          │◄── SENSOR_DATA ────│                                                                          │
          │    (UART Packet)   │                                                                          │

  #### Why this Orchestration Model is Rock-Solid:

  1. Safety Interlocks:
      • The Stepper Motor MCU only moves if the Scale MCU confirms a person is standing on the platform (> 40 kg). If someone steps off mid-
      motion, the Main Controller sends an emergency ABORT_HALT to the stepper immediately.
      • Limit switches at the top and bottom of the vertical gantry prevent the carriage from over-traveling.
  2. Noise and Collision Free:
      • Because the Main Controller acts as the master conductor, only one ESP-NOW packet travels across the 2.4 GHz airwaves at any given
      millisecond. There are zero packet collisions.
  3. Immediate Local Visual Feedback:
      • While the patient is standing straight, the TM1637 LED display mounted on the carriage shows their temperature right at eye-level (36.
      7C), giving them reassuring visual confirmation.
  4. Seamless Transition to Console:
      • While the physical carriage returns to home position, the user places their finger on the console's MAX30102 sensor for pulse and blood
      oxygen.

  ──────
  ### Phase 4: Manual Blood Pressure Input on the 7" Touchscreen

  • Some vitals (like Blood Pressure) are not gathered automatically by the machine.
  • As soon as the automated readings are posted, the 7" screen's Blood Pressure card activates.
  • The patient or nurse inputs Systolic and Diastolic using the on-screen steppers or keyboard.
  • The screen dynamically calculates the American Heart Association (AHA) category:
      • 120 / 80 → Optimal (Green)
      • 135 / 85 → Stage 1 Hypertension (Yellow)
      • 145 / 95 → Stage 2 Hypertension (Orange)
      • > 180 / 120 → Hypertensive Crisis (Red Alert)

  ──────
  ### Phase 5: Dual-Store Persistence (Offline-First)

  1. User taps "Save to Cloud".
  2. Step 1 — Local SQLite on SD Card:
      • The Main Controller writes the complete health record (weight, height, bmi, hr, spo2, temp, systolic, diastolic, timestamp) directly to
      /sdcard/medibot.db.
      • Why: If the clinic loses Wi-Fi or internet connection, zero patient data is ever lost.
  3. Step 2 — Cloud Sync (Firebase REST):
      • The Main Controller pushes the record via HTTPS SSL to Firebase under /patients/{medical_id}/readings/.
      • If the network is down, the record is flagged sync_pending = 1 in SQLite, and the background synchronization worker pushes it
      automatically the moment Wi-Fi reconnects.

  ──────
  ### Phase 6: Closing the Loop on the Web App

  1. The patient logs into the Web App from their phone or computer.
  2. The "Pending Biometric" banner is now gone!
  3. Their dashboard is fully populated with:
      • Today’s health snapshot: Weight, Height, BMI gauge, Blood Pressure, Heart Rate, SpO2, Temperature.
      • Longitudinal progress charts tracking BMI and Blood Pressure over time.
      • PDF export / doctor sharing features.

  ──────
  ### Phase 7: Subsequent Visits (Existing User 2FA Login)

  When the patient visits the kiosk again in the future:

  1. Tap "Existing Patient Login".
  2. Factor 1: Tap RFID Card.
  3. Factor 2: Scan Fingerprint.
  4. With 2FA verified, their personalized dashboard opens immediately.
  5. They tap "Take Measurement" and repeat the orchestrated flow!
  ──────
  Does this orchestration model and state flow capture every detail of what you had in mind? We can now align the ESP-NOW packet structures and
  state machine in medic_bot_hardware to support this exact master-orchestrated protocol.




  # MediBot Health Kiosk: Phase-by-Phase Implementation Plan

This implementation plan details the step-by-step engineering roadmap to implement the complete MediBot clinical workflow, spanning the Web Application, Central Main Controller Orchestrator, Sunton 7" Touchscreen Display, and Distributed ESP-NOW Peripheral Nodes.

---

## System Architecture & Data Topology

```mermaid
flowchart TD
    subgraph Web_Layer["Web & Cloud Ecosystem"]
        Admin["Admin Portal\n(Issues RFID & ID)"]
        WebApp["Patient Web App\n(Sign-up & Records)"]
        Firebase[("Firebase Realtime DB\n(Cloud Master)")]
    end

    subgraph Kiosk_Core["Kiosk Central Core (medic_bot_hardware)"]
        MainESP["Main Controller (ESP32-S3)\nCentral Orchestrator"]
        LocalDB[("SD Card SQLite\n(Offline Resilient)")]
        ConsoleSensors["Console Peripherals:\n• MFRC522 RFID Reader\n• AS608 Biometric FP\n• MAX30102 Pulse Ox\n• MP3 Voice Guidance"]
    end

    subgraph Display_Unit["Interactive Display (display_firmware)"]
        SuntonLCD["Sunton 7\" Touchscreen\n(800x480 RGB + GT911)\n• Live Vitals Dashboard\n• Dynamic BMI Gauge\n• Manual BP Touch Entry"]
    end

    subgraph Wireless_Nodes["Distributed ESP-NOW Peripheral Nodes"]
        WeightNode["Node 1: Base Weight Scale\n(Load Cell + HX711)"]
        HeightNode["Node 2: Height Module\n(Ultrasonic / Sonar)"]
        StepperNode["Node 3: Vertical Gantry\n(TB6600 + Stepper Motor)"]
        TempNode["Node 4: Forehead Carriage\n(IR Temp + TM1637 Display)"]
    end

    Admin -->|Assigns RFID / ID| WebApp
    WebApp <-->|HTTPS REST| Firebase
    Firebase <-->|HTTPS Sync| MainESP
    MainESP <-->|SPI| LocalDB
    MainESP <-->|GPIO / I2C / UART| ConsoleSensors
    MainESP <==>|UART 115200| SuntonLCD
    MainESP <==>|Master ESP-NOW Protocol| WeightNode
    MainESP <==>|Master ESP-NOW Protocol| HeightNode
    MainESP <==>|Master ESP-NOW Protocol| StepperNode
    MainESP <==>|Master ESP-NOW Protocol| TempNode
```

---

## Phase 1: Unified Protocol & Data Schema Definition

### 1.1 Goal
Standardize all data packets across UART, ESP-NOW, and Cloud databases to ensure 100% interoperability before writing code.

### 1.2 Tasks
1. **ESP-NOW Master-Worker Binary Protocol**:
   - Define a compact, fixed-byte struct with opcode multiplexing:
     ```c
     typedef enum {
         NODE_MAIN_CONTROLLER = 0x00,
         NODE_WEIGHT_SCALE    = 0x01,
         NODE_HEIGHT_SONAR    = 0x02,
         NODE_STEPPER_GANTRY  = 0x03,
         NODE_TEMP_CARRIAGE   = 0x04
     } espnow_node_id_t;

     typedef enum {
         CMD_PING             = 0x10,
         CMD_GET_WEIGHT       = 0x20,
         CMD_GET_HEIGHT       = 0x30,
         CMD_MOVE_CARRIAGE    = 0x40,
         CMD_MEASURE_TEMP     = 0x50,
         CMD_RETURN_HOME      = 0x60,
         CMD_EMERGENCY_HALT   = 0xEE,
         RESP_DATA_REPORT     = 0x80,
         RESP_STATUS_ACK      = 0x81
     } espnow_cmd_t;
     ```
2. **UART Protocol Harmonization (`display_comm`)**:
   - Ensure all JSON verbs match across both firmwares:
     - Display $\to$ Main: `START_LOGIN`, `START_ENROLLMENT`, `CARD_SCANNED`, `START_VITALS_MEASUREMENT`, `SAVE_READINGS`, `CANCEL`.
     - Main $\to$ Display: `SYSTEM_STATUS`, `BOOT_PROGRESS`, `USER_DATA`, `SENSOR_DATA`, `FINGERPRINT_PROMPT`, `FINGERPRINT_SUCCESS`, `FINGERPRINT_ERROR`.
3. **Database Schema (Firebase & SQLite)**:
   - Unified record schema:
     ```json
     {
       "medical_id": "MB-94021",
       "rfid_uid": "4A3F129B",
       "name": "Sarah Jenkins",
       "email": "sarah.j@clinic.org",
       "phone": "+2348012345678",
       "dob": "1998-05-14",
       "gender": "Female",
       "biometric_enrolled": true,
       "fingerprint_slot": 14,
       "readings": [
         {
           "timestamp": 1725894120,
           "weight_kg": 68.5,
           "height_cm": 175.0,
           "bmi": 22.4,
           "heart_rate_bpm": 74.0,
           "spo2_pct": 98.5,
           "temperature_c": 36.7,
           "systolic_mmhg": 120,
           "diastolic_mmhg": 80,
           "aha_category": "Optimal"
         }
       ]
     }
     ```

---

## Phase 2: Web Application Integration (`medic-bot_webApp`)

### 2.1 Goal
Enable admin issuance, patient sign-up, biometric activation banners, and cloud synchronization.

### 2.2 Tasks
1. **Patient Sign-Up Flow**:
   - Add fields to the web registration form: Name, Phone, RFID UID, Medical ID, DOB, Gender, Password.
   - On registration, initialize `biometric_enrolled: false` and `readings: []`.
2. **Dashboard Conditional State & Alert Banner**:
   - If `biometric_enrolled == false`:
     - Show an amber callout banner: *"Action Required: Visit the MediBot Kiosk to complete your fingerprint biometric registration."*
     - Hide vitals trends until the first kiosk checkup is recorded.
   - If `biometric_enrolled == true`:
     - Render historical biometric charts, latest BMI gauge, AHA Blood Pressure classifications, and longitudinal history.

---

## Phase 3: Main Controller Orchestrator (`medic_bot_hardware`)

### 3.1 Goal
Transform the Main Controller into the authoritative state machine coordinating RFID auth, dual-scan biometric enrollment, ESP-NOW master queries, and dual-store persistence.

### 3.2 Tasks
1. **Orchestration Finite State Machine (FSM)**:
   - States: `STATE_STANDBY`, `STATE_NEW_USER_ENROLL`, `STATE_LOGIN_2FA`, `STATE_MEASUREMENT_ORCHESTRATION`, `STATE_SAVING`.
2. **New User Biometric Enrollment Sequence**:
   - Receive RFID UID from MFRC522.
   - Fetch user record from Cloud/SD to display name and ID.
   - Run 2-step enrollment using `medicbot::getFingerprintSensor()`:
     - Step 1: `getImage()` $\to$ `image2Tz(1)`.
     - Step 2: Lift finger $\to$ `getImage()` $\to$ `image2Tz(2)`.
     - Step 3: `createModel()` $\to$ `storeModel(1, free_slot)`.
   - Update patient record with `biometric_enrolled = true`.
3. **Master ESP-NOW Orchestrator Engine**:
   - `CMD_GET_WEIGHT` $\to$ wait for scale reading.
   - `CMD_GET_HEIGHT` $\to$ wait for top ultrasonic reading.
   - `CMD_MOVE_CARRIAGE` $\to$ command Stepper to position at forehead.
   - `CMD_MEASURE_TEMP` $\to$ command Carriage IR sensor, illuminate local TM1637 display, receive temperature.
   - In parallel: sample console MAX30102 oximeter.
   - Compute $\text{BMI} = \text{weight} / (\text{height})^2$.
   - Stream aggregated `SENSOR_DATA` to 7" display over UART.
4. **Dual-Store Persistence Engine**:
   - Write immediately to SQLite on SD card (`/sdcard/medibot.db`).
   - Push to Firebase; if network unavailable, tag `sync_pending = 1` and sync when Wi-Fi returns.

---

## Phase 4: Sunton 7" Touchscreen Interface (`display_firmware`)

### 4.1 Goal
Provide intuitive user flows for New User Biometric Registration, 2FA Login, live Dashboard visualization, and Manual Blood Pressure entry.

### 4.2 Tasks
1. **New User Registration Screen Flow**:
   - Step 1: RFID Badge scan prompt showing scanned UID.
   - Step 2: "User Found" demographic card (Name, Age, Medical ID).
   - Step 3: Visual finger placement animation for Scan 1 & Scan 2 with status checkmarks.
2. **Interactive Dashboard & On-Screen Manual BP Input**:
   - Animate the BMI Speedometer gauge and vitals cards.
   - Blood Pressure interactive card:
     - Touch steppers (`+` / `-`) and on-screen numeric keypad for direct input.
     - Live AHA category badge calculation (*Optimal*, *Elevated*, *Stage 1*, *Stage 2*, *Crisis*).
3. **"Save to Cloud" Button**:
   - Sends finalized vitals payload to Main Controller via UART.
   - Displays animated "Record Saved Successfully" toast.

---

## Phase 5: Distributed ESP-NOW Peripheral Nodes

### 5.1 Goal
Equip each specialized peripheral microcontroller with firmware answering the master orchestrator's commands.

### 5.2 Tasks
1. **Weight Scale Node (HX711)**:
   - Remains silent until receiving `CMD_GET_WEIGHT`.
   - Takes 5-sample moving average, verifies stability ($>40\text{ kg}$), transmits weight packet, returns to sleep.
2. **Height Sensor Node (Top Ultrasonic)**:
   - Remains silent until receiving `CMD_GET_HEIGHT`.
   - Takes echo readings, calculates height, transmits single packet, turns off ping.
3. **Stepper Motor Gantry Node (TB6600)**:
   - Receives target height ($H\text{ cm}$).
   - Smoothly steps motor with acceleration profile to forehead level.
   - Sends `CARRIAGE_AT_FOREHEAD_ACK`.
   - On `CMD_RETURN_HOME`, returns carriage to bottom home switch.
4. **Forehead Carriage & TM1637 Display Node**:
   - Receives `CMD_MEASURE_TEMP`.
   - Reads non-contact infrared sensor.
   - Immediately displays `[ 36.7C ]` on the 4-digit TM1637 display at eye level.
   - Transmits temperature packet to Main Controller.

---

## Phase 6: System Integration, Safety & Fail-Safe Verification

### 6.1 Goal
Perform full hardware-in-the-loop testing, safety interlocks, and network failure tolerance.

### 6.2 Tasks
1. **Mechanical & Hardware Safety Interlocks**:
   - Emergency abort if patient steps off scale ($<10\text{ kg}$) while carriage is moving.
   - Hardware limit switches at upper/lower gantry extremes.
2. **Network Resilience Testing**:
   - Cut Wi-Fi power $\to$ confirm full measurement completes, saves to SQLite SD card, and automatically uploads to Firebase once Wi-Fi is restored.
3. **Clinical End-to-End Verification**:
   - Perform full patient journey: Admin registration $\to$ Web App sign-up $\to$ Kiosk biometric enrollment $\to$ Vitals measurement $\to$ Manual BP entry $\to$ Web dashboard review.

---

## Phased Implementation Sequence

| Phase | Description | Key Deliverables |
|:---:|:---|:---|
| **Phase 1** | Unified Protocol & Schemas | ESP-NOW packet structs, UART JSON dictionary, SQLite & Firebase schema |
| **Phase 2** | Web App Enhancements | Registration fields, "Pending Biometrics" banner, Vitals charts |
| **Phase 3** | Main Controller Orchestrator | ESP-NOW master sequencer, 2-scan AS608 enrollment, SD+Cloud sync |
| **Phase 4** | Sunton 7" Screen Flows | Biometric onboarding UI, 2FA screen, Manual BP keyboard/steppers |
| **Phase 5** | Peripheral Node Firmware | Scale, Height, Stepper, and Carriage Temp + TM1637 firmwares |
| **Phase 6** | Integration & Safety Testing | Mechanical interlocks, offline tolerance testing, end-to-end rehearsal |


/home/dshogun8/.gemini/antigravity-cli/brain/d5d86eec-1e96-4645-9773-2d954f22d156/medibot_phase_by_phase_implementation_plan.md
/home/dshogun8/.gemini/antigravity-cli/brain/d5d86eec-1e96-4645-9773-2d954f22d156/medibot_hardware_integration_and_flashing_guide.md