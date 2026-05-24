# MediBot SD Card Database Architecture

## System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         MediBot System Architecture                      │
│                         with SD Card Database                            │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│                          ESP32 Main Controller                           │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────┐   │
│  │                    MEDIC_BOT_CONTROL_MAIN                       │   │
│  │                                                                  │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐        │   │
│  │  │   RFID       │  │ Fingerprint  │  │  Oximeter    │        │   │
│  │  │   Module     │  │   Module     │  │   Module     │        │   │
│  │  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘        │   │
│  │         │                  │                  │                 │   │
│  │         └──────────────────┴──────────────────┘                 │   │
│  │                            │                                     │   │
│  │                            ▼                                     │   │
│  │                  ┌─────────────────┐                           │   │
│  │                  │  User Login &   │                           │   │
│  │                  │  Data Collection│                           │   │
│  │                  └────────┬────────┘                           │   │
│  │                           │                                     │   │
│  │         ┌─────────────────┼─────────────────┐                 │   │
│  │         │                 │                 │                 │   │
│  │         ▼                 ▼                 ▼                 │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │   │
│  │  │ SD Database │  │   Firebase   │  │   Display   │         │   │
│  │  │   Module    │  │    Module    │  │    UART     │         │   │
│  │  └─────┬───────┘  └─────┬───────┘  └─────────────┘         │   │
│  │        │                 │                                     │   │
│  └────────┼─────────────────┼─────────────────────────────────────┘   │
│           │                 │                                           │
│           ▼                 ▼                                           │
│  ┌─────────────────┐  ┌──────────────────┐                           │
│  │   SD Card       │  │   WiFi/Internet  │                           │
│  │   (FAT32)       │  │                  │                           │
│  │                 │  │                  │                           │
│  │  medibot.db     │  │   Firebase       │                           │
│  │  (SQLite)       │  │   Realtime DB    │                           │
│  └─────────────────┘  └──────────────────┘                           │
└─────────────────────────────────────────────────────────────────────────┘
```

## Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          User Login Flow                                 │
└─────────────────────────────────────────────────────────────────────────┘

    User Scans RFID
         │
         ▼
    ┌─────────────────┐
    │ Check Local DB  │◄─────── FAST (20ms)
    │ getUserByRFID() │
    └────────┬────────┘
             │
        ┌────┴────┐
        │ Found?  │
        └────┬────┘
             │
      ┌──────┴──────┐
      │             │
     YES           NO
      │             │
      ▼             ▼
  ┌────────┐   ┌──────────────┐
  │ Return │   │ Query        │◄─────── SLOW (2-5s)
  │ User   │   │ Firebase     │
  │ Data   │   └──────┬───────┘
  └────────┘          │
                      ▼
                 ┌──────────┐
                 │ Save to  │
                 │ Local DB │
                 └──────────┘


┌─────────────────────────────────────────────────────────────────────────┐
│                      Health Reading Flow                                 │
└─────────────────────────────────────────────────────────────────────────┘

    Take Measurements
         │
         ▼
    ┌─────────────────────┐
    │ Save to Local DB    │◄─────── ALWAYS (60ms)
    │ saveHealthReading() │
    │ synced = FALSE      │
    └──────────┬──────────┘
               │
               ▼
    ┌─────────────────────┐
    │ Try Upload to       │
    │ Firebase            │
    └──────────┬──────────┘
               │
          ┌────┴────┐
          │Success? │
          └────┬────┘
               │
        ┌──────┴──────┐
        │             │
       YES           NO
        │             │
        ▼             ▼
    ┌────────┐   ┌──────────┐
    │ Mark   │   │ Keep in  │
    │ Synced │   │ Local DB │
    └────────┘   └──────────┘
                      │
                      ▼
              ┌──────────────┐
              │ Background   │
              │ Sync Task    │
              │ (every 60s)  │
              └──────────────┘


┌─────────────────────────────────────────────────────────────────────────┐
│                      Background Sync Process                             │
└─────────────────────────────────────────────────────────────────────────┘

    Timer (60 seconds)
         │
         ▼
    ┌─────────────────────┐
    │ Check Firebase      │
    │ Connection          │
    └──────────┬──────────┘
               │
          ┌────┴────┐
          │Online?  │
          └────┬────┘
               │
        ┌──────┴──────┐
        │             │
       YES           NO
        │             │
        ▼             ▼
    ┌────────────┐  ┌────────┐
    │ Get        │  │ Wait   │
    │ Unsynced   │  │ & Retry│
    │ Readings   │  └────────┘
    └──────┬─────┘
           │
           ▼
    ┌────────────┐
    │ Upload to  │
    │ Firebase   │
    │ (max 10)   │
    └──────┬─────┘
           │
           ▼
    ┌────────────┐
    │ Mark as    │
    │ Synced     │
    └────────────┘
```

## Database Schema Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          SQLite Database Schema                          │
│                          File: /sd/medibot.db                            │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│                              users TABLE                                 │
├─────────────────────────────────────────────────────────────────────────┤
│ rfid                    TEXT PRIMARY KEY                                 │
│ name                    TEXT NOT NULL                                    │
│ first_name              TEXT                                             │
│ last_name               TEXT                                             │
│ email                   TEXT                                             │
│ age                     TEXT                                             │
│ gender                  TEXT                                             │
│ medical_id              TEXT                                             │
│ fingerprint_registered  INTEGER (0/1)                                    │
│ fingerprint_id          INTEGER (1-127)                                  │
│ created_at              TEXT (timestamp)                                 │
│ last_login              TEXT (timestamp)                                 │
│ login_count             INTEGER                                          │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ 1:N relationship
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         health_readings TABLE                            │
├─────────────────────────────────────────────────────────────────────────┤
│ id                      INTEGER PRIMARY KEY AUTOINCREMENT                │
│ rfid                    TEXT (FOREIGN KEY → users.rfid)                  │
│ timestamp               TEXT                                             │
│ heart_rate              REAL (bpm)                                       │
│ spo2                    REAL (%)                                         │
│ temperature             REAL (°F)                                        │
│ weight                  REAL (kg)                                        │
│ height                  REAL (m)                                         │
│ bmi                     REAL                                             │
│ systolic                INTEGER (mmHg)                                   │
│ diastolic               INTEGER (mmHg)                                   │
│ synced_to_firebase      INTEGER (0/1)                                    │
└─────────────────────────────────────────────────────────────────────────┘

INDEXES:
  - idx_readings_rfid      ON health_readings(rfid)
  - idx_readings_timestamp ON health_readings(timestamp)
  - idx_readings_synced    ON health_readings(synced_to_firebase)
```

## Hardware Connection Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    SD Card Module → ESP32 Wiring                         │
└─────────────────────────────────────────────────────────────────────────┘

    SD Card Module                      ESP32 Main Controller
    ┌──────────────┐                    ┌──────────────────┐
    │              │                    │                  │
    │   [SD CARD]  │                    │                  │
    │              │                    │                  │
    │  ┌────────┐  │                    │                  │
    │  │ CS     │──┼────────────────────┼──► GPIO 5       │
    │  │ MOSI   │──┼────────────────────┼──► GPIO 23      │
    │  │ MISO   │──┼────────────────────┼──► GPIO 19      │
    │  │ SCK    │──┼────────────────────┼──► GPIO 18      │
    │  │ VCC    │──┼────────────────────┼──► 3.3V         │
    │  │ GND    │──┼────────────────────┼──► GND          │
    │  └────────┘  │                    │                  │
    │              │                    │                  │
    └──────────────┘                    └──────────────────┘

    Notes:
    - Use short wires (< 15cm) for reliable SPI communication
    - Add 10kΩ pull-up resistors on MISO, MOSI, SCK if needed
    - Ensure stable 3.3V power supply (SD cards can draw 200mA)
    - Use quality SD card (Class 10 or higher)
```

## Module Interaction Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      Module Interaction Flow                             │
└─────────────────────────────────────────────────────────────────────────┘

┌──────────────┐
│ RFID Module  │
└──────┬───────┘
       │ Scan RFID
       ▼
┌──────────────────────┐
│ Main Controller      │
│ fetchUserData()      │
└──────┬───────────────┘
       │
       ├──► ┌─────────────────┐
       │    │ SD Database     │
       │    │ getUserByRFID() │◄──── Check Local First
       │    └─────────────────┘
       │
       └──► ┌─────────────────┐
            │ Firebase        │
            │ getString()     │◄──── Fallback if not local
            └─────────────────┘
                    │
                    ▼
            ┌─────────────────┐
            │ SD Database     │
            │ saveUser()      │◄──── Cache for next time
            └─────────────────┘

┌──────────────────┐
│ Fingerprint      │
│ Module           │
└──────┬───────────┘
       │ Verify
       ▼
┌──────────────────────┐
│ Main Controller      │
│ updateFingerprint()  │
└──────┬───────────────┘
       │
       ├──► ┌─────────────────┐
       │    │ SD Database     │
       │    │ updateUser...() │◄──── Update Local
       │    └─────────────────┘
       │
       └──► ┌─────────────────┐
            │ Firebase        │
            │ setBool()       │◄──── Sync to Cloud
            └─────────────────┘

┌──────────────────┐
│ Oximeter Module  │
└──────┬───────────┘
       │ Read Vitals
       ▼
┌──────────────────────┐
│ Main Controller      │
│ writeFirebaseDB()    │
└──────┬───────────────┘
       │
       ├──► ┌─────────────────┐
       │    │ SD Database     │
       │    │ saveReading()   │◄──── Save Local (Always)
       │    └─────────────────┘
       │
       └──► ┌─────────────────┐
            │ Firebase        │
            │ setFloat()      │◄──── Upload (If Online)
            └─────────────────┘
                    │
                    ▼
            ┌─────────────────┐
            │ SD Database     │
            │ markSynced()    │◄──── Update Sync Status
            └─────────────────┘
```

## Storage Layout

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        SD Card File Structure                            │
└─────────────────────────────────────────────────────────────────────────┘

SD Card (FAT32)
│
├── /sd/
│   │
│   ├── medibot.db                    ← Main database file
│   │   ├── users table               (~200 bytes per user)
│   │   └── health_readings table     (~150 bytes per reading)
│   │
│   ├── backup_2025_01_21.db         ← Backup files
│   ├── backup_2025_01_20.db
│   │
│   ├── exports/                      ← CSV exports
│   │   ├── user_A1B2C3D4.csv
│   │   ├── user_B2C3D4E5.csv
│   │   └── monthly_report.csv
│   │
│   └── logs/                         ← System logs (future)
│       ├── system_2025_01_21.log
│       └── errors_2025_01_21.log

Storage Estimates:
  - Empty database: ~8 KB
  - 100 users: ~20 KB
  - 5000 readings: ~750 KB
  - Total for typical deployment: < 1 MB
```

---

**Diagram Version**: 1.0  
**Last Updated**: 2025-01-21  
**Format**: ASCII Art for maximum compatibility
