#ifndef SD_DATABASE_MODULE_H
#define SD_DATABASE_MODULE_H

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <sqlite3.h>
#include "config.h"

// SD Card pins are now defined in config.h:
// SD_CS_PIN, SD_MOSI_PIN, SD_MISO_PIN, SD_SCK_PIN

// Database file path
#define DB_PATH "/sd/medibot.db"

// User record structure
typedef struct {
  String rfid;
  String name;
  String first_name;
  String last_name;
  String email;
  String age;
  String gender;
  String medical_id;
  bool fingerprint_registered;
  uint8_t fingerprint_id;
  String last_login;
  int login_count;
} UserRecord;

// Health reading structure
typedef struct {
  String rfid;
  String timestamp;
  float heart_rate;
  float spo2;
  float temperature;
  float weight;
  float height;
  float bmi_sonar;
  float bmi_laser;
  int systolic;
  int diastolic;
  bool synced_to_firebase;
} HealthReading;

// Module functions
bool initSDCard();
bool initDatabase();
void closeDatabase();

// User management
bool saveUser(const UserRecord& user);
bool getUserByRFID(const String& rfid, UserRecord& user);
bool updateUserFingerprint(const String& rfid, uint8_t fingerprintID);
bool updateUserLoginTime(const String& rfid);
int getUserLoginCount(const String& rfid);
bool deleteUser(const String& rfid);
int getAllUsersCount();

// Health readings management
bool saveHealthReading(const HealthReading& reading);
bool getLatestReading(const String& rfid, HealthReading& reading);
int getReadingsCount(const String& rfid);
bool getReadingsByDateRange(const String& rfid, const String& startDate, const String& endDate, HealthReading* readings, int maxCount, int& actualCount);
bool markReadingAsSynced(const String& rfid, const String& timestamp);
bool getUnsyncedReadings(HealthReading* readings, int maxCount, int& actualCount);
bool deleteOldReadings(int daysToKeep);

// Statistics and analytics
bool getUserHealthStats(const String& rfid, float& avgHR, float& avgSpO2, float& avgBMI, int& totalReadings);
bool getSystemStats(int& totalUsers, int& totalReadings, int& unsyncedReadings);

// Database maintenance
bool vacuumDatabase();
bool backupDatabase(const String& backupPath);
bool exportToCSV(const String& rfid, const String& csvPath);

// Utility functions
String getCurrentTimestamp();
bool isDatabaseHealthy();
void printDatabaseInfo();

#endif
