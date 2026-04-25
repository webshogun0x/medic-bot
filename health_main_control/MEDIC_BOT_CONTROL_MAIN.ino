/*
  iDEPP PROJECTS 2024: MEDIC-BOT 2024
  MEDIC-BOT CONTROL UNIT FIRMWARE
  DATE: 10/30/2024
*/

#include "config.h"
#include "esp_now_module.h"
#include "fingerprint_module.h"
#include "rfid_module.h"
#include "oximeter_module.h"
#include "sd_database_module.h"
#include "voice_guidance.h"
#include <Wire.h>

// Firebase includes - only in main file
#include <Arduino.h>
#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif

#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <time.h>
#include "esp_sntp.h"

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
unsigned long sendDataPrevMillis = 0;
int count = 0;
bool signupOK = false;

// User data structure
typedef struct {
  char name[32];
  char first_name[32];
  char last_name[32];
  char email[64];
  char rfid[16];
  char age[16];
  char gender[12];
  char medical_id[16];
  bool isLoggedIn;
} UserData;

// Display data structure for ESP-NOW (if needed, though mostly sent via JSON)
typedef struct {
  char message_type[16];  // "PROMPT", "USER_DATA", "SENSOR_DATA"
  char message[64];
  char user_name[32];
  char user_age[16];
  char user_gender[12];
  double heart_rate;
  double spo2;
  double temperature;
  double weight;
  double height;
  double bmi;
} DisplayData;

// Global system variables - optimized
int fingerprint_count, rfid_count;
float oximeter_temp, user_height_laser, user_height_sonar, user_weight;
float user_bmi_laser, user_bmi_sonar, user_tempa, user_tempo, motor_height;
int user_systolic = 0, user_diastolic = 0;
char weightStatus[8], tempaStatus[8], tempoStatus[8], heightLaserStatus[8], heightSonarStatus[8];
char bmiLaserStatus[8], bmiSonarStatus[8];
UserData currentUser;
// DisplayData removed - not used

// State tracking for back button
enum SystemState {
  STATE_IDLE,
  STATE_LOGIN,
  STATE_ENROLLMENT,
  STATE_DASHBOARD
};

SystemState currentState = STATE_IDLE;
SystemState previousState = STATE_IDLE;
bool cancelCurrentProcess = false;


// Display UART communication (GPIO 6=TX, GPIO 7=RX)
HardwareSerial displaySerial(2);

void initWiFi() {
  WiFi.mode(WIFI_STA);
  Serial.println(F("Initializing WiFi..."));
  voicePlayTrack(VOICE_WIFI_CONNECTING);  // Blocking
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  unsigned long wifiConnectTimeout = millis();
  bool connected = false;

  // Wait for connection with a timeout
  while (millis() - wifiConnectTimeout < 30000) { // 30 seconds timeout
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      break;
    }
    Serial.print(F("."));
    delay(500);
  }
  
  if (connected) {
    Serial.println(F("\nWiFi connected"));
    Serial.println(WiFi.localIP());
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"type\":\"SYSTEM_STATUS\",\"message\":\"WiFi connected\",\"wifi_connected\":true,\"ip\":\"%s\"}\n", WiFi.localIP().toString().c_str());
    displaySerial.print(buf);
  } else {
    Serial.println(F("\nWiFi connection failed!"));
    voicePlayTrack(VOICE_ERROR_WIFI_FAILED);  // Blocking
    displaySerial.print(F("{\"type\":\"SYSTEM_STATUS\",\"message\":\"WiFi connection failed\",\"wifi_connected\":false,\"ip\":\"0.0.0.0\"}\n"));
  }
  delay(300);
}

void ensureWiFiConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WiFi disconnected. Reconnecting..."));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    unsigned long reconnectTimeout = millis();
    bool reconnected = false;

    while (millis() - reconnectTimeout < 30000) { // 30 seconds timeout for reconnection
      if (WiFi.status() == WL_CONNECTED) {
        reconnected = true;
        break;
      }
      Serial.print(".");
      delay(500);
    }
  }
}

void initFirebase() {
  Serial.println(F("Initializing Firebase..."));

  configTime(3600, 0, "pool.ntp.org");
  
  time_t now = time(nullptr);
  uint8_t timeout = 0;
  while (now < 100000 && timeout < 10) {
    delay(300);
    now = time(nullptr);
    timeout++;
  }
  Serial.println(F("Time synced"));

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.timeout.serverResponse = 10000;
  
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  
  Serial.println(F("Signing in..."));
  displaySerial.print(F("{\"type\":\"SYSTEM_STATUS\",\"message\":\"Firebase connecting\",\"firebase_connecting\":true}\n"));
  delay(300);
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  uint8_t authTimeout = 0;
  while (auth.token.uid == "" && authTimeout < 15) {
    delay(500);
    authTimeout++;
  }
  
  signupOK = (auth.token.uid != "");
  Serial.println(signupOK ? F("Firebase OK") : F("Firebase timeout"));
  
  if (signupOK) {
    voicePlayTrack(VOICE_SYSTEM_READY);  // Blocking
  }
  
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"type\":\"SYSTEM_STATUS\",\"message\":\"Firebase %s\",\"firebase_connected\":%s,\"ready\":%s}\n", 
           signupOK ? "connected" : "failed", signupOK ? "true" : "false", signupOK ? "true" : "false");
  displaySerial.print(buf);
  delay(300);
}

void writePath(String data_path, String status_path, String data, String status) {
  if (Firebase.RTDB.setString(&fbdo, data_path, data)) {
    Serial.println(F("PASSED"));
  } else {
    Serial.print(F("FAILED: ")); Serial.println(fbdo.errorReason().c_str());
  }
  if (Firebase.RTDB.setString(&fbdo, status_path, status)) {
    Serial.println(F("PASSED"));
  } else {
    Serial.print(F("FAILED: ")); Serial.println(fbdo.errorReason().c_str());
  }
}

UserData fetchUserData(String rfidNumber) {
  UserData user;
  memset(&user, 0, sizeof(UserData));
  user.isLoggedIn = false;
  rfidNumber.trim();
  strncpy(user.rfid, rfidNumber.c_str(), sizeof(user.rfid) - 1);
  
  UserRecord localUser;
  if (getUserByRFID(rfidNumber, localUser)) {
    Serial.print(F("User found in local database: ")); Serial.println(localUser.name.c_str());
    strncpy(user.name, localUser.name.c_str(), sizeof(user.name) - 1);
    strncpy(user.first_name, localUser.first_name.c_str(), sizeof(user.first_name) - 1);
    strncpy(user.last_name, localUser.last_name.c_str(), sizeof(user.last_name) - 1);
    strncpy(user.email, localUser.email.c_str(), sizeof(user.email) - 1);
    strncpy(user.age, localUser.age.c_str(), sizeof(user.age) - 1);
    strncpy(user.gender, localUser.gender.c_str(), sizeof(user.gender) - 1);
    strncpy(user.medical_id, localUser.medical_id.c_str(), sizeof(user.medical_id) - 1);
    
    updateUserLoginTime(rfidNumber);
    return user;
  }
  
  ensureWiFiConnected();
  uint8_t fbTimeout = 0;
  while (!Firebase.ready() && fbTimeout < 20) {
    delay(500);
    fbTimeout++;
  }
  
  if (Firebase.ready()) {
    String basePath = "USERS/" + rfidNumber;
    
    if (Firebase.RTDB.getString(&fbdo, basePath + "/name")) {
      strncpy(user.name, fbdo.stringData().c_str(), sizeof(user.name) - 1);
      Serial.print(F("User found in Firebase: ")); Serial.println(user.name);
      
      if (Firebase.RTDB.getString(&fbdo, basePath + "/first_name")) strncpy(user.first_name, fbdo.stringData().c_str(), sizeof(user.first_name) - 1);
      if (Firebase.RTDB.getString(&fbdo, basePath + "/last_name")) strncpy(user.last_name, fbdo.stringData().c_str(), sizeof(user.last_name) - 1);
      if (Firebase.RTDB.getString(&fbdo, basePath + "/email")) strncpy(user.email, fbdo.stringData().c_str(), sizeof(user.email) - 1);
      if (Firebase.RTDB.getString(&fbdo, basePath + "/age")) strncpy(user.age, fbdo.stringData().c_str(), sizeof(user.age) - 1);
      if (Firebase.RTDB.getString(&fbdo, basePath + "/gender")) strncpy(user.gender, fbdo.stringData().c_str(), sizeof(user.gender) - 1);
      if (Firebase.RTDB.getString(&fbdo, basePath + "/medical_id")) strncpy(user.medical_id, fbdo.stringData().c_str(), sizeof(user.medical_id) - 1);
      
      UserRecord newUser;
      newUser.rfid = rfidNumber;
      newUser.name = user.name;
      newUser.first_name = user.first_name;
      newUser.last_name = user.last_name;
      newUser.email = user.email;
      newUser.age = user.age;
      newUser.gender = user.gender;
      newUser.medical_id = user.medical_id;
      newUser.fingerprint_registered = false;
      newUser.fingerprint_id = 0;
      saveUser(newUser);
      updateUserLoginTime(rfidNumber);
      return user;
    }
  }

  // Fallback
  // strncpy(user.name, "Sir_timmy", sizeof(user.name)-1);
  // strncpy(user.first_name, "Timileyin", sizeof(user.first_name)-1);
  // strncpy(user.last_name, "Idowu", sizeof(user.last_name)-1);
  // strncpy(user.email, "timmy@example.com", sizeof(user.email)-1);
  // strncpy(user.age, "25 years", sizeof(user.age)-1);
  // strncpy(user.gender, "Male", sizeof(user.gender)-1);
  // strncpy(user.medical_id, "MED001", sizeof(user.medical_id)-1);
  
  // return user;
}

void sendPrompt(const char* message) {
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"type\":\"PROMPT\",\"message\":\"%s\"}\n", message);
  displaySerial.print(buf);
}

void sendUserData() {
  char buf[256];
  snprintf(buf, sizeof(buf), 
    "{\"type\":\"USER_DATA\",\"user_name\":\"%s\",\"first_name\":\"%s\",\"last_name\":\"%s\","
    "\"email\":\"%s\",\"user_medical_id\":\"%s\",\"user_age\":\"%s\",\"user_gender\":\"%s\"}\n",
    currentUser.name, currentUser.first_name, currentUser.last_name,
    currentUser.email, currentUser.medical_id, currentUser.age, currentUser.gender);
  displaySerial.print(buf);
}

void sendSensorData() {
  char buf[256];
  snprintf(buf, sizeof(buf), 
    "{\"type\":\"SENSOR_DATA\",\"heart_rate\":%.1f,\"spo2\":%.1f,\"temperature\":%.1f,"
    "\"weight\":%.1f,\"height\":%.2f,\"bmi\":%.1f}\n",
    user_hr, user_sp02, user_tempo, user_weight, user_height_laser, user_bmi_laser);
  displaySerial.print(buf);
}

void sendSimpleMessage(const char* msgType, const char* message) {
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"type\":\"%s\",\"message\":\"%s\"}\n", msgType, message);
  displaySerial.print(buf);
}

bool updateFingerprintStatus(String rfidNumber, bool status, uint8_t fingerprintID) {
  // Update local database first
  updateUserFingerprint(rfidNumber, fingerprintID);
  
  ensureWiFiConnected();
  
  // Wait for Firebase to be ready
  uint8_t fbTimeout = 0;
  while (!Firebase.ready() && fbTimeout < 20) {
    delay(500);
    fbTimeout++;
  }
  
  if (Firebase.ready()) {
    String basePath = "USERS/" + rfidNumber;
    bool success = true;
    
    if (!Firebase.RTDB.setBool(&fbdo, basePath + "/fingerprint_registered", status)) {
      Serial.println("Failed to update fingerprint_registered: " + fbdo.errorReason());
      success = false;
    }
    
    if (!Firebase.RTDB.setInt(&fbdo, basePath + "/fingerprint_id", fingerprintID)) {
      Serial.println("Failed to update fingerprint_id: " + fbdo.errorReason());
      success = false;
    }
    
    return success;
  }
  Serial.println("Firebase not ready, saved to local database only");
  return true; // Return true since local save succeeded
}

uint8_t getFingerprintIDFromFirebase(String rfidNumber) {
  // Try local database first
  UserRecord localUser;
  if (getUserByRFID(rfidNumber, localUser)) {
    if (localUser.fingerprint_registered) {
      Serial.printf("Fingerprint ID from local DB: %d\n", localUser.fingerprint_id);
      return localUser.fingerprint_id;
    }
  }
  
  // Fallback to Firebase
  ensureWiFiConnected();
  
  // Wait for Firebase to be ready
  uint8_t fbTimeout = 0;
  while (!Firebase.ready() && fbTimeout < 20) {
    delay(500);
    fbTimeout++;
  }
  
  if (Firebase.ready()) {
    String path = "USERS/" + rfidNumber + "/fingerprint_id";
    if (Firebase.RTDB.getInt(&fbdo, path)) {
      return (uint8_t)fbdo.intData();
    }
  }
  Serial.println("Cannot get fingerprint ID");
  return 0;
}

uint8_t rfidToFingerprintID(String rfidNumber) {
  // Improved hash function using CRC-like algorithm to reduce collisions
  unsigned long rfidHash = 0x811C9DC5; // FNV-1a offset basis
  
  for (int i = 0; i < rfidNumber.length(); i++) {
    rfidHash ^= (unsigned long)rfidNumber[i];
    rfidHash *= 0x01000193; // FNV-1a prime
  }
  
  // Map to 1-127 range (0 is reserved)
  uint8_t fingerprintID = (rfidHash % 127) + 1;
  
  Serial.print("RFID: ");
  Serial.print(rfidNumber);
  Serial.print(" -> Fingerprint ID: ");
  Serial.println(fingerprintID);
  
  return fingerprintID;
}


void enrollmentProcess() {
  ensureWiFiConnected();
  Serial.println("=== ENROLLMENT ===");
  voicePlayTrackNoWait(VOICE_ENROLL_SCAN_RFID);  // Non-blocking
  sendPrompt("Please scan your RFID card");
  
  Serial.println("Entering RFID scan loop...");
  tidString = "NIL";
  int loopCount = 0;
  while (tidString == "NIL" && !cancelCurrentProcess) {
    readRFID();
    delay(100);
    loopCount++;
    if (loopCount % 50 == 0) {  // Print every 5 seconds
      Serial.print("Waiting for RFID... (");
      Serial.print(loopCount / 10);
      Serial.println(" seconds)");
    }
  }
  
  if (cancelCurrentProcess) {
    cancelCurrentProcess = false;
    Serial.println("Enrollment cancelled by user");
    return;
  }
  
  Serial.println("RFID Detected: " + tidString);
  String rfidMsg = "RFID Detected: " + tidString;
  sendPrompt(rfidMsg.c_str());
  voicePlayTrackNoWait(VOICE_ENROLL_RFID_DETECTED);  // Non-blocking
  
  currentUser = fetchUserData(tidString);
  Serial.print("User: "); Serial.println(currentUser.name);
  
  if (currentUser.name == "Unknown") {
    Serial.println("ERROR: RFID not registered in system!");
    voicePlayTrackNoWait(VOICE_ERROR_USER_NOT_FOUND);  // Non-blocking
    sendPrompt("ERROR: RFID not registered!");
    return;
  }
  
  uint8_t fingerprintID = rfidToFingerprintID(tidString);
  Serial.println("Assigned Fingerprint ID: " + String(fingerprintID));
  String fpMsg = "Fingerprint ID: " + String(fingerprintID);
  sendPrompt(fpMsg.c_str());
  
  id = fingerprintID;
  Serial.print("Starting fingerprint enrollment for "); Serial.print(currentUser.name); Serial.println("...");
  String enrollMsg = String("Starting enrollment for ") + currentUser.name;
  sendPrompt(enrollMsg.c_str());
  
  if (getFingerprintEnroll()) {
    Serial.print("SUCCESS: Fingerprint enrolled for "); Serial.println(currentUser.name);
    voicePlayTrackNoWait(VOICE_ENROLL_SUCCESS);  // Non-blocking
    sendPrompt("SUCCESS: Enrollment complete!");
    
    if (updateFingerprintStatus(tidString, true, fingerprintID)) {
      Serial.println("Database updated: Fingerprint ID " + String(fingerprintID) + " registered!");
      sendPrompt("Database updated successfully");
    } else {
      Serial.println("WARNING: Database update failed");
      sendPrompt("WARNING: Database update failed");
    }
    
    // After successful enrollment, notify display
    currentUser.isLoggedIn = true;
    previousState = currentState;
    currentState = STATE_DASHBOARD;
    delay(2000);
    voicePlayTrackNoWait(VOICE_DASHBOARD_INSTRUCTIONS);  // Non-blocking
    sendUserData();
    Serial.println("Enrollment complete - user logged in");
  } else {
    Serial.println("FAILED: Fingerprint enrollment unsuccessful");
    voicePlayTrackNoWait(VOICE_ENROLL_FAILED);  // Non-blocking
    sendPrompt("FAILED: Enrollment unsuccessful");
  }
  
  Serial.println("=== ENROLLMENT COMPLETE ===");
}

// void checkDatabaseStructure() {
  // Serial.println("=== CHECKING DATABASE STRUCTURE ===");
  // 
  // if (Firebase.ready()) {
    // //Check if USERS node exists
    // if (Firebase.RTDB.get(&fbdo, "USERS")) {
      // Serial.println("USERS node exists");
      // Serial.println("Data type: " + fbdo.dataType());
      // 
      // if (fbdo.dataType() == "json") {
        // Serial.println("Available users in database:");
        // Serial.println(fbdo.jsonString());
      // }
    // } else {
      // Serial.println("USERS node does not exist!");
      // Serial.println("Error: " + fbdo.errorReason());
    // }
    // 
    // //Try to list all children under USERS
    // Serial.println("\nTrying to get USERS children...");
    // if (Firebase.RTDB.getJSON(&fbdo, "USERS")) {
      // Serial.println("USERS JSON data:");
      // Serial.println(fbdo.jsonString());
    // }
  // } else {
    // Serial.println("Firebase not ready for database check!");
  // }
  // 
  // Serial.println("=== DATABASE CHECK COMPLETE ===");
// }


void loginProcess() {
  ensureWiFiConnected();
  Serial.println("=== LOGIN ===");
  voicePlayTrackNoWait(VOICE_LOGIN_SCAN_RFID);  // Non-blocking
  sendPrompt("Please scan your RFID card...");
  
  Serial.println("Entering RFID scan loop...");
  tidString = "NIL";
  int loopCount = 0;
  while (tidString == "NIL" && !cancelCurrentProcess) {
    readRFID();
    delay(100);
    loopCount++;
    if (loopCount % 50 == 0) {  // Print every 5 seconds
      Serial.print("Waiting for RFID... (");
      Serial.print(loopCount / 10);
      Serial.println(" seconds)");
    }
  }
  
  if (cancelCurrentProcess) {
    cancelCurrentProcess = false;
    Serial.println("Login cancelled by user");
    return;
  }
  
  Serial.println("RFID Detected: " + tidString);
  String rfidMsg = "RFID Detected: " + tidString;
  sendPrompt(rfidMsg.c_str());
  voicePlayTrackNoWait(VOICE_LOGIN_RFID_DETECTED);  // Non-blocking
  
  currentUser = fetchUserData(tidString);
  
  if (currentUser.name == "Unknown") {
    Serial.println("ERROR: User not found!");
    voicePlayTrackNoWait(VOICE_ERROR_USER_NOT_FOUND);  // Non-blocking
    sendPrompt("ERROR: User not found!");
    return;
  }
  
  sendPrompt("Please scan fingerprint...");
  voicePlayTrackNoWait(VOICE_LOGIN_FINGERPRINT_SCANNING);  // Non-blocking
  delay(2000);
  
  // Get stored fingerprint ID from Firebase
  uint8_t expectedID = getFingerprintIDFromFirebase(tidString);
  
  if (expectedID == 0) {
    Serial.println("ERROR: No fingerprint registered for this user!");
    sendSimpleMessage("FINGERPRINT_ERROR", "No fingerprint registered. Please enroll first.");
    return;
  }
  
  Serial.println("Expected Fingerprint ID: " + String(expectedID));
  
  int fingerprintResult = getFingerprintIDez();
  Serial.println("Scanned Fingerprint ID: " + String(fingerprintResult));
  
  if (fingerprintResult == expectedID) {
    currentUser.isLoggedIn = true;
    previousState = currentState;
    currentState = STATE_DASHBOARD;
    Serial.print("LOGIN SUCCESS: Welcome "); Serial.println(currentUser.name);
    voicePlayTrackNoWait(VOICE_LOGIN_SUCCESS);  // Non-blocking
    sendSimpleMessage("FINGERPRINT_SUCCESS", "Fingerprint verified successfully");
    delay(1000);
    voicePlayTrackNoWait(VOICE_DASHBOARD_INSTRUCTIONS);  // Non-blocking
    sendUserData();
    Serial.println("Login complete - user logged in");
    
  } else if (fingerprintResult == -1) {
    Serial.println("LOGIN FAILED: No fingerprint detected");
    voicePlayTrackNoWait(VOICE_LOGIN_FAILED);  // Non-blocking
    sendSimpleMessage("FINGERPRINT_ERROR", "No fingerprint detected. Try again.");
  } else {
    Serial.println("LOGIN FAILED: Fingerprint mismatch");
    voicePlayTrackNoWait(VOICE_LOGIN_FAILED);  // Non-blocking
    sendSimpleMessage("FINGERPRINT_ERROR", "Fingerprint mismatch. Please enroll first.");
  }
}

void writeFirebaseDB() {
  if (currentUser.rfid == "") {
    Serial.println("No user logged in");
    return;
  }
  
  // Save to local SD database first
  HealthReading reading;
  reading.rfid = currentUser.rfid;
  reading.timestamp = getCurrentTimestamp();
  reading.heart_rate = user_hr;
  reading.spo2 = user_sp02;
  reading.temperature = user_tempo;
  reading.weight = user_weight;
  reading.height = user_height_laser;
  reading.bmi_laser = user_bmi_laser;
  reading.bmi_sonar = user_bmi_sonar;
  reading.systolic = user_systolic;
  reading.diastolic = user_diastolic;
  reading.synced_to_firebase = false;
  
  if (saveHealthReading(reading)) {
    Serial.println("Health reading saved to local database");
  }
  
  ensureWiFiConnected();
  
  // Wait for Firebase to be ready
  uint8_t fbTimeout = 0;
  while (!Firebase.ready() && fbTimeout < 20) {
    delay(500);
    fbTimeout++;
  }
  
  if (Firebase.ready()) {
    Serial.println("Saving data to Firebase...");
    
    String userPath = String("READINGS/") + currentUser.rfid + "/latest";
    time_t now = time(nullptr);
    String timestamp = String(now);
    
    bool success = true;
    
    // Save latest readings with error checking
    if (!Firebase.RTDB.setFloat(&fbdo, userPath + "/heart_rate", user_hr)) {
      Serial.println("Failed to save heart_rate: " + fbdo.errorReason());
      success = false;
    }
    if (!Firebase.RTDB.setFloat(&fbdo, userPath + "/spo2", user_sp02)) {
      Serial.println("Failed to save spo2: " + fbdo.errorReason());
      success = false;
    }
    if (!Firebase.RTDB.setFloat(&fbdo, userPath + "/temperature", user_tempo)) {
      Serial.println("Failed to save temperature: " + fbdo.errorReason());
      success = false;
    }
    if (!Firebase.RTDB.setFloat(&fbdo, userPath + "/weight", user_weight)) {
      Serial.println("Failed to save weight: " + fbdo.errorReason());
      success = false;
    }
    if (!Firebase.RTDB.setFloat(&fbdo, userPath + "/height", user_height_laser)) {
      Serial.println("Failed to save height: " + fbdo.errorReason());
      success = false;
    }
    if (!Firebase.RTDB.setFloat(&fbdo, userPath + "/bmi_laser", user_bmi_laser)) {
      Serial.println("Failed to save bmi: " + fbdo.errorReason());
      success = false;
    }
    if (!Firebase.RTDB.setFloat(&fbdo, userPath + "/bmi_sonar", user_bmi_sonar)) {
      Serial.println("Failed to save bmi: " + fbdo.errorReason());
      success = false;
    }
    if (user_systolic > 0 && !Firebase.RTDB.setInt(&fbdo, userPath + "/systolic", user_systolic)) {
      Serial.println("Failed to save systolic: " + fbdo.errorReason());
      success = false;
    }
    if (user_diastolic > 0 && !Firebase.RTDB.setInt(&fbdo, userPath + "/diastolic", user_diastolic)) {
      Serial.println("Failed to save diastolic: " + fbdo.errorReason());
      success = false;
    }
    if (!Firebase.RTDB.setString(&fbdo, userPath + "/timestamp", timestamp)) {
      Serial.println("Failed to save timestamp: " + fbdo.errorReason());
      success = false;
    }
    
    if (success) {
      Serial.println("Data saved to Firebase");
      
      // Mark as synced in local database
      markReadingAsSynced(currentUser.rfid, reading.timestamp);
      
      // Save to history
      String historyPath = String("READINGS/") + currentUser.rfid + "/history/" + timestamp;
      Firebase.RTDB.setFloat(&fbdo, historyPath + "/heart_rate", user_hr);
      Firebase.RTDB.setFloat(&fbdo, historyPath + "/spo2", user_sp02);
      Firebase.RTDB.setFloat(&fbdo, historyPath + "/temperature", user_tempo);
      Firebase.RTDB.setFloat(&fbdo, historyPath + "/bmi_laser", user_bmi_laser);
      Firebase.RTDB.setFloat(&fbdo, historyPath + "/bmi_sonar", user_bmi_sonar);
      if (user_systolic > 0) Firebase.RTDB.setInt(&fbdo, historyPath + "/systolic", user_systolic);
      if (user_diastolic > 0) Firebase.RTDB.setInt(&fbdo, historyPath + "/diastolic", user_diastolic);
    } else {
      Serial.println("Firebase save failed, data kept in local database");
    }
  } else {
    Serial.println("Firebase not ready, data saved to local database only");
  }
}

bool checkSensorTimeout(unsigned long startTime) {
  if (millis() - startTime > 5000) {
    sendPrompt("ERROR: Sensor read timeout. Try again.");
    return true;  // Timeout occurred
  }
  return false;  // No timeout
}

void handleDisplayCommands() {
  if (!displaySerial.available()) return;
  
  static char cmdBuf[128]; // Static buffer reused
  uint8_t idx = 0;
  unsigned long readingTimeOut = millis(); // Reset timeout on new command
  
  // Read command character by character
  while (displaySerial.available() && idx < sizeof(cmdBuf) - 1) {
    char c = displaySerial.read();
    if (c == '\n') break;
    cmdBuf[idx++] = c;
  }
  cmdBuf[idx] = '\0';
  
  // Quick string matching without String class
  if (strstr(cmdBuf, "DISPLAY_READY")) {
      Serial.println("=== DISPLAY READY - SENDING STATUS ===");
      // Resend system status
      if (WiFi.status() == WL_CONNECTED) {
        char statusBuf[128];
        snprintf(statusBuf, sizeof(statusBuf), "{\"type\":\"SYSTEM_STATUS\",\"message\":\"WiFi connected\",\"wifi_connected\":true,\"ip\":\"%s\"}\n", WiFi.localIP().toString().c_str());
        displaySerial.print(statusBuf);
        delay(100);
      }
      if (signupOK) {
        displaySerial.print(F("{\"type\":\"SYSTEM_STATUS\",\"message\":\"Firebase connected\",\"firebase_connected\":true,\"ready\":true}\n"));
        delay(100);
      }
  } else if (strstr(cmdBuf, "STATUS_COMPLETE")) {
      Serial.println("=== STATUS SCREEN COMPLETE - SYSTEM READY ===");
  } else if (strstr(cmdBuf, "BACK")) {
      Serial.println("=== BACK BUTTON PRESSED ===");
      Serial.print("Current state: "); Serial.println(currentState);
      
      if (currentState == STATE_DASHBOARD) {
        previousState = currentState;
        currentState = STATE_IDLE;
        currentUser.isLoggedIn = false;
        sendPrompt("Logged out successfully");
        Serial.println("Returned from dashboard to idle");
      } else if (currentState == STATE_LOGIN || currentState == STATE_ENROLLMENT) {
        cancelCurrentProcess = true;
        previousState = currentState;
        currentState = STATE_IDLE;
        Serial.println("Cancelling current process...");
      }
      Serial.println("=== BACK HANDLED ===");
  } else if (strcmp(cmdBuf, "START_LOGIN") == 0) {
      Serial.println("=== DISPLAY REQUESTED LOGIN ===");
      cancelCurrentProcess = false;
      previousState = currentState;
      currentState = STATE_LOGIN;
      loginProcess();
  } else if (strcmp(cmdBuf, "START_ENROLLMENT") == 0) {
      Serial.println("=== DISPLAY REQUESTED ENROLLMENT ===");
      cancelCurrentProcess = false;
      previousState = currentState;
      currentState = STATE_ENROLLMENT;
      enrollmentProcess();
  } else if (strcmp(cmdBuf, "READ_OXIMETER") == 0) {
      Serial.println("=== READ BUTTON PRESSED ===");
      voicePlayTrackNoWait(VOICE_MEASUREMENT_STEP_ON_SCALE);  // Non-blocking
      
      Serial.println("Reading oximeter...");
      voicePlayTrackNoWait(VOICE_PLACE_FINGER_OXIMETER);  // Non-blocking
      readOximeter();
      voicePlayTrackNoWait(VOICE_READING_OXIMETER);  // Non-blocking
      Serial.print("Heart Rate: "); Serial.println(user_hr);
      Serial.print("SpO2: "); Serial.println(user_sp02);
      
      Serial.println("Reading ESP-NOW data...");
      if (checkSensorTimeout(readingTimeOut)) return;
      voicePlayTrackNoWait(VOICE_MEASURING_HEIGHT);  // Non-blocking
      
      readESPNowData();
      Serial.print("Temperature: "); Serial.println(user_tempo);
      Serial.print("Weight: "); Serial.println(user_weight);
      Serial.print("Height: "); Serial.println(user_height_laser);
      Serial.print("BMI: "); Serial.println(user_bmi_laser);
      if (checkSensorTimeout(readingTimeOut)) return;
      
      voicePlayTrackNoWait(VOICE_MEASUREMENTS_COMPLETE);  // Non-blocking
      sendSensorData();
      Serial.println("=== SENSOR DATA SENT TO DISPLAY ===");
  } else if (strncmp(cmdBuf, "SAVE_READINGS", 13) == 0) {
      Serial.println("=== SAVE BUTTON PRESSED ===");
      
      // Check if command contains blood pressure data
      char* systolicPtr = strstr(cmdBuf, "systolic:");
      char* diastolicPtr = strstr(cmdBuf, "diastolic:");
      
      if (systolicPtr && diastolicPtr) {
        user_systolic = atoi(systolicPtr + 9);
        user_diastolic = atoi(diastolicPtr + 10);
        Serial.print("Blood Pressure - Systolic: "); Serial.print(user_systolic);
        Serial.print(", Diastolic: "); Serial.println(user_diastolic);
      }
      
      Serial.println("Saving readings to Firebase...");
      writeFirebaseDB();
      voicePlayTrackNoWait(VOICE_SAVE_SUCCESS);  // Non-blocking
      sendPrompt("Readings saved successfully");
      Serial.println("=== READINGS SAVED ===");
  } else if (strcmp(cmdBuf, "LOGOUT") == 0) {
      Serial.println("=== LOGOUT BUTTON PRESSED ===");
      Serial.print("User "); Serial.print(currentUser.name); Serial.println(" logging out");
      previousState = currentState;
      currentState = STATE_IDLE;
      currentUser.isLoggedIn = false;
      voicePlayTrackNoWait(VOICE_LOGOUT);  // Non-blocking
      sendPrompt("Logged out successfully");
      Serial.println("=== USER LOGGED OUT ===");
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_I2C, SCL_I2C);
  pinMode(BUZZER, OUTPUT);
  
  delay(1000);

  Serial.println("...Starting Medic-Bot");
  
  // Initialize voice guidance first
  voiceInit();
  voicePlayTrack(VOICE_WELCOME);  // Blocking
  
  // Initialize SD card and database
  if (initSDCard()) {
    Serial.println("SD Card initialized");
    if (initDatabase()) {
      Serial.println("Database initialized");
      printDatabaseInfo();
    } else {
      Serial.println("WARNING: Database initialization failed!");
    }
  } else {
    Serial.println("WARNING: SD Card initialization failed!");
  }
  
  // Initialize display UART
  displaySerial.begin(DISPLAY_BAUD, SERIAL_8N1, DISPLAY_RX, DISPLAY_TX);
  
  // Wait for display to initialize UART receiver
  delay(6000);
  
  initWiFi();
  initESPNow();
  initFingerprint();
  delay(1000);
  initRFID();
  initOximeter();
  delay(1000);
  initFirebase();
  
  Serial.println("System ready - waiting for display commands");
}

void loop() {
  // Always handle display commands
  handleDisplayCommands();
  
  // Periodic background sync of unsynced readings to Firebase
  static unsigned long lastSyncCheck = 0;
  if (millis() - lastSyncCheck > 60000) { // Check every 60 seconds
    lastSyncCheck = millis();
    syncUnsyncedReadings();
  }
  
  delay(100);
}

void syncUnsyncedReadings() {
  if (!Firebase.ready()) return;
  
  HealthReading readings[10]; // Sync up to 10 readings at a time
  int count = 0;
  
  if (getUnsyncedReadings(readings, 10, count)) {
    Serial.printf("Found %d unsynced readings, syncing to Firebase...\n", count);
    
    for (int i = 0; i < count; i++) {
      String historyPath = "READINGS/" + readings[i].rfid + "/history/" + String(time(nullptr));
      
      bool success = true;
      success &= Firebase.RTDB.setFloat(&fbdo, historyPath + "/heart_rate", readings[i].heart_rate);
      success &= Firebase.RTDB.setFloat(&fbdo, historyPath + "/spo2", readings[i].spo2);
      success &= Firebase.RTDB.setFloat(&fbdo, historyPath + "/temperature", readings[i].temperature);
      success &= Firebase.RTDB.setFloat(&fbdo, historyPath + "/weight", readings[i].weight);
      success &= Firebase.RTDB.setFloat(&fbdo, historyPath + "/height", readings[i].height);
      success &= Firebase.RTDB.setFloat(&fbdo, historyPath + "/bmi_sonar", readings[i].bmi_sonar);
      success &= Firebase.RTDB.setFloat(&fbdo, historyPath + "/bmi_laser", readings[i].bmi_laser);
      
      if (success) {
        markReadingAsSynced(readings[i].rfid, readings[i].timestamp);
        Serial.printf("Synced reading for %s at %s\n", readings[i].rfid.c_str(), readings[i].timestamp.c_str());
      }
    }
  }
}