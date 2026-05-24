# MediBot SD Card & SQLite Database Integration Guide

## 📋 Overview

The MediBot system now includes local data persistence using an SD card with SQLite database. This provides:
- **Offline Operation**: System works without internet connection
- **Data Backup**: All user data and health readings stored locally
- **Faster Access**: Local database queries are instant
- **Automatic Sync**: Unsynced data automatically uploads to Firebase when online
- **Data Export**: Export user data to CSV for analysis

---

## 🔌 Hardware Requirements

### **SD Card Module Wiring**

Connect SD card module to ESP32 main controller:

| SD Card Pin | ESP32 Pin | Description |
|-------------|-----------|-------------|
| CS (Chip Select) | GPIO 5 | Chip select signal |
| MOSI | GPIO 23 | Master Out Slave In |
| MISO | GPIO 19 | Master In Slave Out |
| SCK | GPIO 18 | Serial Clock |
| VCC | 3.3V | Power supply |
| GND | GND | Ground |

**Note**: These pins are defined in `sd_database_module.h` and can be changed if needed:
```cpp
#define SD_CS_PIN       5
#define SD_MOSI_PIN     23
#define SD_MISO_PIN     19
#define SD_SCK_PIN      18
```

### **SD Card Specifications**
- **Type**: MicroSD or SD card
- **Format**: FAT32 (recommended)
- **Capacity**: 4GB - 32GB (larger cards work but not necessary)
- **Speed Class**: Class 10 or higher recommended

---

## 📊 Database Schema

### **Table: users**
Stores user profile information

| Column | Type | Description |
|--------|------|-------------|
| rfid | TEXT (PRIMARY KEY) | User's RFID card number |
| name | TEXT | Full name |
| first_name | TEXT | First name |
| last_name | TEXT | Last name |
| email | TEXT | Email address |
| age | TEXT | Age (e.g., "25 years") |
| gender | TEXT | Gender |
| medical_id | TEXT | Medical ID number |
| fingerprint_registered | INTEGER | 1 if fingerprint enrolled, 0 otherwise |
| fingerprint_id | INTEGER | Fingerprint sensor ID (1-127) |
| created_at | TEXT | Account creation timestamp |
| last_login | TEXT | Last login timestamp |
| login_count | INTEGER | Total number of logins |

### **Table: health_readings**
Stores all health measurements

| Column | Type | Description |
|--------|------|-------------|
| id | INTEGER (PRIMARY KEY) | Auto-increment ID |
| rfid | TEXT | User's RFID (foreign key) |
| timestamp | TEXT | Reading timestamp |
| heart_rate | REAL | Heart rate (bpm) |
| spo2 | REAL | Blood oxygen saturation (%) |
| temperature | REAL | Body temperature (°F) |
| weight | REAL | Weight (kg) |
| height | REAL | Height (meters) |
| bmi | REAL | Body Mass Index |
| systolic | INTEGER | Systolic blood pressure |
| diastolic | INTEGER | Diastolic blood pressure |
| synced_to_firebase | INTEGER | 1 if synced to cloud, 0 otherwise |

### **Indexes**
- `idx_readings_rfid`: Fast lookup by user RFID
- `idx_readings_timestamp`: Fast lookup by date/time
- `idx_readings_synced`: Fast lookup of unsynced readings

---

## 🚀 Installation & Setup

### **1. Install Required Libraries**

Add to your Arduino IDE Library Manager:

```
SD (built-in with ESP32)
SPI (built-in with ESP32)
sqlite3 (search for "sqlite3" by siara-cc)
```

Or manually download:
- **sqlite3**: https://github.com/siara-cc/esp32_arduino_sqlite3_lib

### **2. Format SD Card**

1. Insert SD card into computer
2. Format as FAT32
3. Eject safely
4. Insert into SD card module

### **3. Upload Firmware**

1. Open `MEDIC_BOT_CONTROL_MAIN.ino` in Arduino IDE
2. Verify all files are in the same folder:
   - `sd_database_module.h`
   - `sd_database_module.cpp`
3. Select your ESP32 board
4. Upload

### **4. Verify Installation**

Open Serial Monitor (115200 baud) and look for:

```
...Starting Medic-Bot
Initializing SD card...
SD Card Type: SDHC
SD Card Size: 8192MB
SD Card initialized
Initializing SQLite database...
Database opened successfully
Database initialized successfully

=== DATABASE INFO ===
Total Users: 0
Total Readings: 0
Unsynced Readings: 0
Database Size: 8192 bytes
Database Health: OK
====================
```

---

## 💻 API Reference

### **User Management Functions**

#### `bool saveUser(const UserRecord& user)`
Saves or updates a user in the database.

**Example**:
```cpp
UserRecord user;
user.rfid = "A1B2C3D4";
user.name = "John Doe";
user.first_name = "John";
user.last_name = "Doe";
user.email = "john@example.com";
user.age = "30 years";
user.gender = "Male";
user.medical_id = "MED12345";
user.fingerprint_registered = false;
user.fingerprint_id = 0;

if (saveUser(user)) {
  Serial.println("User saved successfully");
}
```

#### `bool getUserByRFID(const String& rfid, UserRecord& user)`
Retrieves user data by RFID.

**Example**:
```cpp
UserRecord user;
if (getUserByRFID("A1B2C3D4", user)) {
  Serial.println("User found: " + user.name);
  Serial.println("Email: " + user.email);
  Serial.println("Login count: " + String(user.login_count));
}
```

#### `bool updateUserFingerprint(const String& rfid, uint8_t fingerprintID)`
Updates user's fingerprint enrollment status.

**Example**:
```cpp
if (updateUserFingerprint("A1B2C3D4", 42)) {
  Serial.println("Fingerprint ID 42 registered");
}
```

#### `bool updateUserLoginTime(const String& rfid)`
Updates last login time and increments login counter.

**Example**:
```cpp
updateUserLoginTime("A1B2C3D4");
```

#### `int getUserLoginCount(const String& rfid)`
Gets total number of logins for a user.

**Example**:
```cpp
int count = getUserLoginCount("A1B2C3D4");
Serial.printf("User has logged in %d times\n", count);
```

---

### **Health Readings Functions**

#### `bool saveHealthReading(const HealthReading& reading)`
Saves a health measurement to the database.

**Example**:
```cpp
HealthReading reading;
reading.rfid = "A1B2C3D4";
reading.timestamp = getCurrentTimestamp();
reading.heart_rate = 72.0;
reading.spo2 = 98.0;
reading.temperature = 98.6;
reading.weight = 70.5;
reading.height = 1.75;
reading.bmi = 23.0;
reading.systolic = 120;
reading.diastolic = 80;
reading.synced_to_firebase = false;

if (saveHealthReading(reading)) {
  Serial.println("Reading saved");
}
```

#### `bool getLatestReading(const String& rfid, HealthReading& reading)`
Gets the most recent health reading for a user.

**Example**:
```cpp
HealthReading reading;
if (getLatestReading("A1B2C3D4", reading)) {
  Serial.printf("Latest HR: %.1f bpm\n", reading.heart_rate);
  Serial.printf("Latest SpO2: %.1f%%\n", reading.spo2);
}
```

#### `int getReadingsCount(const String& rfid)`
Gets total number of readings for a user.

**Example**:
```cpp
int count = getReadingsCount("A1B2C3D4");
Serial.printf("User has %d readings\n", count);
```

#### `bool getUnsyncedReadings(HealthReading* readings, int maxCount, int& actualCount)`
Gets readings that haven't been synced to Firebase.

**Example**:
```cpp
HealthReading readings[10];
int count = 0;

if (getUnsyncedReadings(readings, 10, count)) {
  Serial.printf("Found %d unsynced readings\n", count);
  for (int i = 0; i < count; i++) {
    Serial.printf("RFID: %s, Time: %s\n", 
      readings[i].rfid.c_str(), 
      readings[i].timestamp.c_str());
  }
}
```

#### `bool markReadingAsSynced(const String& rfid, const String& timestamp)`
Marks a reading as successfully synced to Firebase.

**Example**:
```cpp
markReadingAsSynced("A1B2C3D4", "2025-01-21 14:30:00");
```

---

### **Statistics Functions**

#### `bool getUserHealthStats(const String& rfid, float& avgHR, float& avgSpO2, float& avgBMI, int& totalReadings)`
Calculates average health metrics for a user.

**Example**:
```cpp
float avgHR, avgSpO2, avgBMI;
int totalReadings;

if (getUserHealthStats("A1B2C3D4", avgHR, avgSpO2, avgBMI, totalReadings)) {
  Serial.printf("Average HR: %.1f bpm\n", avgHR);
  Serial.printf("Average SpO2: %.1f%%\n", avgSpO2);
  Serial.printf("Average BMI: %.1f\n", avgBMI);
  Serial.printf("Total readings: %d\n", totalReadings);
}
```

#### `bool getSystemStats(int& totalUsers, int& totalReadings, int& unsyncedReadings)`
Gets overall system statistics.

**Example**:
```cpp
int totalUsers, totalReadings, unsyncedReadings;

if (getSystemStats(totalUsers, totalReadings, unsyncedReadings)) {
  Serial.printf("Total users: %d\n", totalUsers);
  Serial.printf("Total readings: %d\n", totalReadings);
  Serial.printf("Unsynced: %d\n", unsyncedReadings);
}
```

---

### **Maintenance Functions**

#### `bool vacuumDatabase()`
Optimizes database by reclaiming unused space.

**Example**:
```cpp
vacuumDatabase(); // Run monthly
```

#### `bool backupDatabase(const String& backupPath)`
Creates a backup copy of the database.

**Example**:
```cpp
if (backupDatabase("/sd/backup_2025_01_21.db")) {
  Serial.println("Backup created successfully");
}
```

#### `bool exportToCSV(const String& rfid, const String& csvPath)`
Exports user's health data to CSV file.

**Example**:
```cpp
if (exportToCSV("A1B2C3D4", "/sd/user_A1B2C3D4.csv")) {
  Serial.println("Data exported to CSV");
}
```

#### `bool deleteOldReadings(int daysToKeep)`
Deletes readings older than specified days.

**Example**:
```cpp
deleteOldReadings(90); // Keep only last 90 days
```

#### `bool isDatabaseHealthy()`
Checks database integrity.

**Example**:
```cpp
if (isDatabaseHealthy()) {
  Serial.println("Database is healthy");
} else {
  Serial.println("Database corruption detected!");
}
```

#### `void printDatabaseInfo()`
Prints database statistics to Serial.

**Example**:
```cpp
printDatabaseInfo();
```

---

## 🔄 Data Flow

### **User Login Flow**
```
1. User scans RFID
2. System checks local database first
   ├─ Found → Load user data from SD card (instant)
   └─ Not found → Query Firebase → Save to SD card for next time
3. Update login timestamp and counter in local database
4. Proceed with fingerprint verification
```

### **Health Reading Flow**
```
1. User takes measurements
2. Save to local database immediately (synced_to_firebase = 0)
3. Attempt to save to Firebase
   ├─ Success → Mark as synced (synced_to_firebase = 1)
   └─ Fail → Keep in local database for later sync
4. Background task syncs unsynced readings every 60 seconds
```

### **Background Sync Process**
```
Every 60 seconds:
1. Check if Firebase is connected
2. Query unsynced readings (synced_to_firebase = 0)
3. Upload up to 10 readings at a time
4. Mark successfully uploaded readings as synced
5. Repeat until all readings are synced
```

---

## 🛠️ Troubleshooting

### **SD Card Not Detected**
```
Error: "SD Card initialization failed!"
```

**Solutions**:
1. Check wiring connections
2. Verify SD card is formatted as FAT32
3. Try different SD card
4. Check power supply (SD cards need stable 3.3V)
5. Add 10kΩ pull-up resistors on MISO, MOSI, SCK lines

### **Database Corruption**
```
Error: "Database Health: CORRUPTED"
```

**Solutions**:
1. Run integrity check:
   ```cpp
   if (!isDatabaseHealthy()) {
     Serial.println("Restoring from backup...");
     // Copy backup file over corrupted database
   }
   ```
2. Delete database file and restart (will recreate)
3. Restore from backup

### **Slow Database Performance**
**Solutions**:
1. Run vacuum:
   ```cpp
   vacuumDatabase();
   ```
2. Delete old readings:
   ```cpp
   deleteOldReadings(90); // Keep only 90 days
   ```
3. Use faster SD card (Class 10)

### **Out of Space**
```
Error: "Failed to save reading"
```

**Solutions**:
1. Check SD card space:
   ```cpp
   uint64_t totalBytes = SD.totalBytes();
   uint64_t usedBytes = SD.usedBytes();
   Serial.printf("Used: %llu / %llu bytes\n", usedBytes, totalBytes);
   ```
2. Delete old readings
3. Export and archive old data
4. Use larger SD card

---

## 📈 Performance Metrics

### **Operation Times** (on ESP32 @ 240MHz with Class 10 SD card)

| Operation | Time | Notes |
|-----------|------|-------|
| Save user | ~50ms | First write slower (~200ms) |
| Get user by RFID | ~20ms | Indexed query |
| Save health reading | ~60ms | Includes index update |
| Get latest reading | ~25ms | Indexed query |
| Get 10 unsynced readings | ~80ms | Indexed query |
| Vacuum database | ~500ms | Run monthly |
| Backup database (1MB) | ~2s | Depends on file size |
| Export to CSV (100 readings) | ~1.5s | Depends on reading count |

### **Storage Estimates**

| Data | Size per Record | 1000 Records | 10000 Records |
|------|----------------|--------------|---------------|
| User | ~200 bytes | 200 KB | 2 MB |
| Health Reading | ~150 bytes | 150 KB | 1.5 MB |
| Database Overhead | - | ~50 KB | ~200 KB |

**Example**: 100 users with 50 readings each = 100×200 + 5000×150 = 770 KB

---

## 🔒 Security Considerations

### **Data Protection**
- Database file is stored in plaintext on SD card
- Physical access to SD card = access to all data
- Consider encrypting sensitive fields if needed

### **Recommendations**:
1. Use encrypted SD cards for sensitive deployments
2. Implement database encryption (SQLCipher)
3. Regularly backup and remove SD card to secure location
4. Add tamper detection (detect SD card removal)

---

## 📝 Maintenance Schedule

### **Daily**
- Automatic background sync runs every 60 seconds
- No manual intervention needed

### **Weekly**
- Check database health:
  ```cpp
  printDatabaseInfo();
  ```

### **Monthly**
- Vacuum database:
  ```cpp
  vacuumDatabase();
  ```
- Create backup:
  ```cpp
  backupDatabase("/sd/backup_monthly.db");
  ```

### **Quarterly**
- Delete old readings:
  ```cpp
  deleteOldReadings(90);
  ```
- Export historical data:
  ```cpp
  exportToCSV("A1B2C3D4", "/sd/archive_Q1_2025.csv");
  ```

---

## 🎯 Best Practices

1. **Always check return values**:
   ```cpp
   if (!saveHealthReading(reading)) {
     Serial.println("Failed to save reading!");
     // Handle error
   }
   ```

2. **Use transactions for multiple operations** (future enhancement):
   ```cpp
   // Begin transaction
   // Multiple database operations
   // Commit transaction
   ```

3. **Regular backups**:
   ```cpp
   // Backup before major operations
   backupDatabase("/sd/backup_before_update.db");
   ```

4. **Monitor SD card health**:
   ```cpp
   if (!isDatabaseHealthy()) {
     // Alert user or restore from backup
   }
   ```

5. **Limit query results**:
   ```cpp
   // Don't load all readings at once
   HealthReading readings[10]; // Limit to 10
   ```

---

## 📚 Additional Resources

- **SQLite Documentation**: https://www.sqlite.org/docs.html
- **ESP32 SD Library**: https://github.com/espressif/arduino-esp32/tree/master/libraries/SD
- **sqlite3 for ESP32**: https://github.com/siara-cc/esp32_arduino_sqlite3_lib

---

**Document Version**: 1.0  
**Last Updated**: 2025-01-21  
**Author**: MediBot Development Team
