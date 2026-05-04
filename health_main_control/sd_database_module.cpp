#include "sd_database_module.h"
#include <time.h>

static sqlite3 *db = NULL;
static bool sd_initialized = false;
static bool db_initialized = false;

// Callback for SELECT queries
static int callback(void *data, int argc, char **argv, char **azColName) {
  int i;
  for (i = 0; i < argc; i++) {
    Serial.printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
  }
  Serial.println();
  return 0;
}

// Execute SQL query
static bool executeSQL(const char* sql) {
  char *errMsg = 0;
  int rc = sqlite3_exec(db, sql, callback, 0, &errMsg);
  
  if (rc != SQLITE_OK) {
    Serial.printf("SQL error: %s\n", errMsg);
    sqlite3_free(errMsg);
    return false;
  }
  return true;
}

bool initSDCard() {
  Serial.println("Initializing SD card...");
  
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD Card initialization failed!");
    return false;
  }
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return false;
  }
  
  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  
  sd_initialized = true;
  return true;
}

bool initDatabase() {
  if (!sd_initialized) {
    Serial.println("SD card not initialized!");
    return false;
  }
  
  Serial.println("Initializing SQLite database...");
  
  // Open database
  int rc = sqlite3_open(DB_PATH, &db);
  if (rc) {
    Serial.printf("Can't open database: %s\n", sqlite3_errmsg(db));
    return false;
  }
  
  Serial.println("Database opened successfully");
  
  // Create USERS table
  const char* createUsersTable = 
    "CREATE TABLE IF NOT EXISTS users ("
    "rfid TEXT PRIMARY KEY,"
    "name TEXT NOT NULL,"
    "first_name TEXT,"
    "last_name TEXT,"
    "email TEXT,"
    "age TEXT,"
    "gender TEXT,"
    "medical_id TEXT,"
    "fingerprint_registered INTEGER DEFAULT 0,"
    "fingerprint_id INTEGER DEFAULT 0,"
    "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
    "last_login TEXT,"
    "login_count INTEGER DEFAULT 0"
    ");";
  
  if (!executeSQL(createUsersTable)) {
    Serial.println("Failed to create users table");
    return false;
  }
  
  // Create HEALTH_READINGS table
  const char* createReadingsTable = 
    "CREATE TABLE IF NOT EXISTS health_readings ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "rfid TEXT NOT NULL,"
    "timestamp TEXT NOT NULL,"
    "heart_rate REAL,"
    "spo2 REAL,"
    "temperature REAL,"
    "weight REAL,"
    "height REAL,"
    "bmi_sonar REAL,"
    "bmi_laser REAL,"
    "systolic INTEGER,"
    "diastolic INTEGER,"
    "synced_to_firebase INTEGER DEFAULT 0,"
    "FOREIGN KEY (rfid) REFERENCES users(rfid)"
    ");";
  
  if (!executeSQL(createReadingsTable)) {
    Serial.println("Failed to create health_readings table");
    return false;
  }
  
  // Create indexes for faster queries
  executeSQL("CREATE INDEX IF NOT EXISTS idx_readings_rfid ON health_readings(rfid);");
  executeSQL("CREATE INDEX IF NOT EXISTS idx_readings_timestamp ON health_readings(timestamp);");
  executeSQL("CREATE INDEX IF NOT EXISTS idx_readings_synced ON health_readings(synced_to_firebase);");
  
  Serial.println("Database initialized successfully");
  db_initialized = true;
  return true;
}

void closeDatabase() {
  if (db) {
    sqlite3_close(db);
    db = NULL;
    db_initialized = false;
    Serial.println("Database closed");
  }
}

bool saveUser(const UserRecord& user) {
  if (!db_initialized) return false;
  
  char sql[1024];
  snprintf(sql, sizeof(sql),
    "INSERT OR REPLACE INTO users "
    "(rfid, name, first_name, last_name, email, age, gender, medical_id, "
    "fingerprint_registered, fingerprint_id, login_count) "
    "VALUES ('%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', %d, %d, "
    "COALESCE((SELECT login_count FROM users WHERE rfid='%s'), 0));",
    user.rfid.c_str(), user.name.c_str(), user.first_name.c_str(),
    user.last_name.c_str(), user.email.c_str(), user.age.c_str(),
    user.gender.c_str(), user.medical_id.c_str(),
    user.fingerprint_registered ? 1 : 0, user.fingerprint_id,
    user.rfid.c_str());
  
  bool result = executeSQL(sql);
  if (result) {
    Serial.printf("User %s saved to database\n", user.rfid.c_str());
  }
  return result;
}

bool getUserByRFID(const String& rfid, UserRecord& user) {
  if (!db_initialized) return false;
  
  char sql[256];
  snprintf(sql, sizeof(sql),
    "SELECT * FROM users WHERE rfid='%s';", rfid.c_str());
  
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    Serial.printf("Failed to prepare statement: %s\n", sqlite3_errmsg(db));
    return false;
  }
  
  bool found = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    user.rfid = String((const char*)sqlite3_column_text(stmt, 0));
    user.name = String((const char*)sqlite3_column_text(stmt, 1));
    user.first_name = String((const char*)sqlite3_column_text(stmt, 2));
    user.last_name = String((const char*)sqlite3_column_text(stmt, 3));
    user.email = String((const char*)sqlite3_column_text(stmt, 4));
    user.age = String((const char*)sqlite3_column_text(stmt, 5));
    user.gender = String((const char*)sqlite3_column_text(stmt, 6));
    user.medical_id = String((const char*)sqlite3_column_text(stmt, 7));
    user.fingerprint_registered = sqlite3_column_int(stmt, 8) == 1;
    user.fingerprint_id = sqlite3_column_int(stmt, 9);
    user.last_login = String((const char*)sqlite3_column_text(stmt, 11));
    user.login_count = sqlite3_column_int(stmt, 12);
    found = true;
  }
  
  sqlite3_finalize(stmt);
  return found;
}

bool updateUserFingerprint(const String& rfid, uint8_t fingerprintID) {
  if (!db_initialized) return false;
  
  char sql[256];
  snprintf(sql, sizeof(sql),
    "UPDATE users SET fingerprint_registered=1, fingerprint_id=%d WHERE rfid='%s';",
    fingerprintID, rfid.c_str());
  
  return executeSQL(sql);
}

bool updateUserLoginTime(const String& rfid) {
  if (!db_initialized) return false;
  
  char sql[256];
  snprintf(sql, sizeof(sql),
    "UPDATE users SET last_login=CURRENT_TIMESTAMP, login_count=login_count+1 WHERE rfid='%s';",
    rfid.c_str());
  
  return executeSQL(sql);
}

int getUserLoginCount(const String& rfid) {
  if (!db_initialized) return 0;
  
  char sql[256];
  snprintf(sql, sizeof(sql),
    "SELECT login_count FROM users WHERE rfid='%s';", rfid.c_str());
  
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) return 0;
  
  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }
  
  sqlite3_finalize(stmt);
  return count;
}

bool deleteUser(const String& rfid) {
  if (!db_initialized) return false;
  
  // Delete user's readings first
  char sql[256];
  snprintf(sql, sizeof(sql), "DELETE FROM health_readings WHERE rfid='%s';", rfid.c_str());
  executeSQL(sql);
  
  // Delete user
  snprintf(sql, sizeof(sql), "DELETE FROM users WHERE rfid='%s';", rfid.c_str());
  return executeSQL(sql);
}

int getAllUsersCount() {
  if (!db_initialized) return 0;
  
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM users;", -1, &stmt, 0);
  
  if (rc != SQLITE_OK) return 0;
  
  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }
  
  sqlite3_finalize(stmt);
  return count;
}

bool saveHealthReading(const HealthReading& reading) {
  if (!db_initialized) return false;
  
  char sql[1024];
  snprintf(sql, sizeof(sql),
    "INSERT INTO health_readings "
    "(rfid, timestamp, heart_rate, spo2, temperature, weight, height, bmi, systolic, diastolic, synced_to_firebase) "
    "VALUES ('%s', '%s', %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %d, %d, %d);",
    reading.rfid.c_str(), reading.timestamp.c_str(),
    reading.heart_rate, reading.spo2, reading.temperature,
    reading.weight, reading.height, reading.bmi_sonar, reading.bmi_laser,
    reading.systolic, reading.diastolic,
    reading.synced_to_firebase ? 1 : 0);
  
  bool result = executeSQL(sql);
  if (result) {
    Serial.printf("Health reading saved for user %s\n", reading.rfid.c_str());
  }
  return result;
}

bool getLatestReading(const String& rfid, HealthReading& reading) {
  if (!db_initialized) return false;
  
  char sql[256];
  snprintf(sql, sizeof(sql),
    "SELECT * FROM health_readings WHERE rfid='%s' ORDER BY timestamp DESC LIMIT 1;",
    rfid.c_str());
  
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) return false;
  
  bool found = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    reading.rfid = String((const char*)sqlite3_column_text(stmt, 1));
    reading.timestamp = String((const char*)sqlite3_column_text(stmt, 2));
    reading.heart_rate = sqlite3_column_double(stmt, 3);
    reading.spo2 = sqlite3_column_double(stmt, 4);
    reading.temperature = sqlite3_column_double(stmt, 5);
    reading.weight = sqlite3_column_double(stmt, 6);
    reading.height = sqlite3_column_double(stmt, 7);
    reading.bmi_sonar = sqlite3_column_double(stmt, 8);
    reading.bmi_laser = sqlite3_column_double(stmt, 9);
    reading.systolic = sqlite3_column_int(stmt, 10);
    reading.diastolic = sqlite3_column_int(stmt, 11);
    reading.synced_to_firebase = sqlite3_column_int(stmt, 12) == 1;
    found = true;
  }
  
  sqlite3_finalize(stmt);
  return found;
}

int getReadingsCount(const String& rfid) {
  if (!db_initialized) return 0;
  
  char sql[256];
  snprintf(sql, sizeof(sql),
    "SELECT COUNT(*) FROM health_readings WHERE rfid='%s';", rfid.c_str());
  
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) return 0;
  
  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }
  
  sqlite3_finalize(stmt);
  return count;
}

bool markReadingAsSynced(const String& rfid, const String& timestamp) {
  if (!db_initialized) return false;
  
  char sql[256];
  snprintf(sql, sizeof(sql),
    "UPDATE health_readings SET synced_to_firebase=1 WHERE rfid='%s' AND timestamp='%s';",
    rfid.c_str(), timestamp.c_str());
  
  return executeSQL(sql);
}

bool getUnsyncedReadings(HealthReading* readings, int maxCount, int& actualCount) {
  if (!db_initialized) return false;
  
  char sql[256];
  snprintf(sql, sizeof(sql),
    "SELECT * FROM health_readings WHERE synced_to_firebase=0 ORDER BY timestamp ASC LIMIT %d;",
    maxCount);
  
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) return false;
  
  actualCount = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && actualCount < maxCount) {
    readings[actualCount].rfid = String((const char*)sqlite3_column_text(stmt, 1));
    readings[actualCount].timestamp = String((const char*)sqlite3_column_text(stmt, 2));
    readings[actualCount].heart_rate = sqlite3_column_double(stmt, 3);
    readings[actualCount].spo2 = sqlite3_column_double(stmt, 4);
    readings[actualCount].temperature = sqlite3_column_double(stmt, 5);
    readings[actualCount].weight = sqlite3_column_double(stmt, 6);
    readings[actualCount].height = sqlite3_column_double(stmt, 7);
    readings[actualCount].bmi_sonar = sqlite3_column_double(stmt, 8);
    readings[actualCount].bmi_laser = sqlite3_column_double(stmt, 9);
    readings[actualCount].systolic = sqlite3_column_int(stmt, 10);
    readings[actualCount].diastolic = sqlite3_column_int(stmt, 11);
    readings[actualCount].synced_to_firebase = false;
    actualCount++;
  }
  
  sqlite3_finalize(stmt);
  return actualCount > 0;
}

bool deleteOldReadings(int daysToKeep) {
  if (!db_initialized) return false;
  
  char sql[256];
  snprintf(sql, sizeof(sql),
    "DELETE FROM health_readings WHERE timestamp < datetime('now', '-%d days');",
    daysToKeep);
  
  return executeSQL(sql);
}

bool getUserHealthStats(const String& rfid, float& avgHR, float& avgSpO2, float& avgBMI, int& totalReadings) {
  if (!db_initialized) return false;
  
  char sql[256];
  snprintf(sql, sizeof(sql),
    "SELECT AVG(heart_rate), AVG(spo2), AVG(bmi), COUNT(*) FROM health_readings WHERE rfid='%s';",
    rfid.c_str());
  
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) return false;
  
  bool found = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    avgHR = sqlite3_column_double(stmt, 0);
    avgSpO2 = sqlite3_column_double(stmt, 1);
    avgBMI = sqlite3_column_double(stmt, 2);
    totalReadings = sqlite3_column_int(stmt, 3);
    found = true;
  }
  
  sqlite3_finalize(stmt);
  return found;
}

bool getSystemStats(int& totalUsers, int& totalReadings, int& unsyncedReadings) {
  if (!db_initialized) return false;
  
  sqlite3_stmt *stmt;
  
  // Get total users
  sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM users;", -1, &stmt, 0);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    totalUsers = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  
  // Get total readings
  sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM health_readings;", -1, &stmt, 0);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    totalReadings = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  
  // Get unsynced readings
  sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM health_readings WHERE synced_to_firebase=0;", -1, &stmt, 0);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    unsyncedReadings = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  
  return true;
}

bool vacuumDatabase() {
  if (!db_initialized) return false;
  Serial.println("Vacuuming database...");
  return executeSQL("VACUUM;");
}

bool backupDatabase(const String& backupPath) {
  if (!sd_initialized) return false;
  
  Serial.printf("Backing up database to %s...\n", backupPath.c_str());
  
  File source = SD.open(DB_PATH, FILE_READ);
  if (!source) {
    Serial.println("Failed to open source database");
    return false;
  }
  
  File dest = SD.open(backupPath.c_str(), FILE_WRITE);
  if (!dest) {
    source.close();
    Serial.println("Failed to create backup file");
    return false;
  }
  
  uint8_t buffer[512];
  while (source.available()) {
    int bytesRead = source.read(buffer, sizeof(buffer));
    dest.write(buffer, bytesRead);
  }
  
  source.close();
  dest.close();
  
  Serial.println("Backup completed successfully");
  return true;
}

bool exportToCSV(const String& rfid, const String& csvPath) {
  if (!db_initialized || !sd_initialized) return false;
  
  Serial.printf("Exporting data to %s...\n", csvPath.c_str());
  
  File csvFile = SD.open(csvPath.c_str(), FILE_WRITE);
  if (!csvFile) {
    Serial.println("Failed to create CSV file");
    return false;
  }
  
  // Write CSV header
  csvFile.println("Timestamp,Heart Rate,SpO2,Temperature,Weight,Height,BMI,Systolic,Diastolic");
  
  // Query readings
  char sql[256];
  snprintf(sql, sizeof(sql),
    "SELECT timestamp, heart_rate, spo2, temperature, weight, height, bmi, systolic, diastolic "
    "FROM health_readings WHERE rfid='%s' ORDER BY timestamp;",
    rfid.c_str());
  
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  
  if (rc != SQLITE_OK) {
    csvFile.close();
    return false;
  }
  
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    csvFile.printf("%s,%.1f,%.1f,%.1f,%.1f,%.2f,%.1f,%d,%d\n",
      (const char*)sqlite3_column_text(stmt, 0),
      sqlite3_column_double(stmt, 1),
      sqlite3_column_double(stmt, 2),
      sqlite3_column_double(stmt, 3),
      sqlite3_column_double(stmt, 4),
      sqlite3_column_double(stmt, 5),
      sqlite3_column_double(stmt, 6),
      sqlite3_column_int(stmt, 7),
      sqlite3_column_int(stmt, 8));
  }
  
  sqlite3_finalize(stmt);
  csvFile.close();
  
  Serial.println("Export completed successfully");
  return true;
}

String getCurrentTimestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  
  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

bool isDatabaseHealthy() {
  if (!db_initialized) return false;
  
  // Run integrity check
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, "PRAGMA integrity_check;", -1, &stmt, 0);
  
  if (rc != SQLITE_OK) return false;
  
  bool healthy = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* result = (const char*)sqlite3_column_text(stmt, 0);
    healthy = (strcmp(result, "ok") == 0);
  }
  
  sqlite3_finalize(stmt);
  return healthy;
}

void printDatabaseInfo() {
  if (!db_initialized) {
    Serial.println("Database not initialized");
    return;
  }
  
  Serial.println("\n=== DATABASE INFO ===");
  
  int totalUsers, totalReadings, unsyncedReadings;
  if (getSystemStats(totalUsers, totalReadings, unsyncedReadings)) {
    Serial.printf("Total Users: %d\n", totalUsers);
    Serial.printf("Total Readings: %d\n", totalReadings);
    Serial.printf("Unsynced Readings: %d\n", unsyncedReadings);
  }
  
  // Get database file size
  File dbFile = SD.open(DB_PATH, FILE_READ);
  if (dbFile) {
    Serial.printf("Database Size: %d bytes\n", dbFile.size());
    dbFile.close();
  }
  
  Serial.printf("Database Health: %s\n", isDatabaseHealthy() ? "OK" : "CORRUPTED");
  Serial.println("====================\n");
}
