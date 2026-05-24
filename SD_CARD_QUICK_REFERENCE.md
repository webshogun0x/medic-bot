# MediBot SD Card Database - Quick Reference

## 🔌 Hardware Setup
```
SD Card → ESP32
CS   → GPIO 5
MOSI → GPIO 23
MISO → GPIO 19
SCK  → GPIO 18
VCC  → 3.3V
GND  → GND
```

## 📦 Required Libraries
```cpp
#include <SD.h>
#include <SPI.h>
#include <sqlite3.h>
#include "sd_database_module.h"
```

## 🚀 Initialization
```cpp
void setup() {
  if (initSDCard()) {
    if (initDatabase()) {
      Serial.println("Database ready!");
      printDatabaseInfo();
    }
  }
}
```

## 👤 User Operations

### Save User
```cpp
UserRecord user;
user.rfid = "A1B2C3D4";
user.name = "John Doe";
user.email = "john@example.com";
saveUser(user);
```

### Get User
```cpp
UserRecord user;
if (getUserByRFID("A1B2C3D4", user)) {
  Serial.println(user.name);
}
```

### Update Login
```cpp
updateUserLoginTime("A1B2C3D4");
int count = getUserLoginCount("A1B2C3D4");
```

## 📊 Health Readings

### Save Reading
```cpp
HealthReading reading;
reading.rfid = "A1B2C3D4";
reading.timestamp = getCurrentTimestamp();
reading.heart_rate = 72.0;
reading.spo2 = 98.0;
reading.synced_to_firebase = false;
saveHealthReading(reading);
```

### Get Latest
```cpp
HealthReading reading;
if (getLatestReading("A1B2C3D4", reading)) {
  Serial.printf("HR: %.1f\n", reading.heart_rate);
}
```

### Sync Unsynced
```cpp
HealthReading readings[10];
int count = 0;
if (getUnsyncedReadings(readings, 10, count)) {
  // Upload to Firebase
  markReadingAsSynced(readings[0].rfid, readings[0].timestamp);
}
```

## 📈 Statistics
```cpp
// User stats
float avgHR, avgSpO2, avgBMI;
int total;
getUserHealthStats("A1B2C3D4", avgHR, avgSpO2, avgBMI, total);

// System stats
int users, readings, unsynced;
getSystemStats(users, readings, unsynced);
```

## 🛠️ Maintenance

### Backup
```cpp
backupDatabase("/sd/backup.db");
```

### Export CSV
```cpp
exportToCSV("A1B2C3D4", "/sd/user_data.csv");
```

### Cleanup
```cpp
vacuumDatabase();              // Optimize
deleteOldReadings(90);         // Keep 90 days
```

### Health Check
```cpp
if (isDatabaseHealthy()) {
  Serial.println("DB OK");
}
printDatabaseInfo();
```

## 🔄 Data Flow

**Login**: Local DB → Firebase (if not found) → Save to Local  
**Reading**: Save Local → Upload Firebase → Mark Synced  
**Sync**: Every 60s, upload unsynced readings

## ⚠️ Error Handling
```cpp
if (!saveHealthReading(reading)) {
  Serial.println("Save failed!");
  // Retry or log error
}

if (!isDatabaseHealthy()) {
  Serial.println("DB corrupted!");
  // Restore from backup
}
```

## 📏 Storage Estimates
- User: ~200 bytes
- Reading: ~150 bytes
- 100 users + 5000 readings ≈ 770 KB

## 🎯 Best Practices
1. ✅ Always check return values
2. ✅ Regular backups (monthly)
3. ✅ Vacuum database (monthly)
4. ✅ Delete old readings (quarterly)
5. ✅ Monitor database health (weekly)

## 🐛 Common Issues

**SD Not Detected**: Check wiring, format FAT32  
**Slow Performance**: Run `vacuumDatabase()`  
**Out of Space**: Delete old readings  
**Corruption**: Restore from backup

## 📞 Quick Diagnostics
```cpp
// Check SD card
uint64_t total = SD.totalBytes();
uint64_t used = SD.usedBytes();
Serial.printf("SD: %llu / %llu MB\n", used/1024/1024, total/1024/1024);

// Check database
printDatabaseInfo();

// Check health
if (!isDatabaseHealthy()) {
  Serial.println("ALERT: Database corrupted!");
}
```

---
**Version**: 1.0 | **Date**: 2025-01-21
