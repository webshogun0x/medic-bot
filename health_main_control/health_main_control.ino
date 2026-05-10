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

// Global system variables - optimized
int fingerprint_count, rfid_count;
float oximeter_temp, user_height_laser, user_height_sonar, user_weight;
float user_bmi_laser, user_bmi_sonar, user_tempa, user_tempo, motor_height;
int user_systolic = 0, user_diastolic = 0;
char weightStatus[8], tempaStatus[8], tempoStatus[8], heightLaserStatus[8], heightSonarStatus[8];
char bmiLaserStatus[8], bmiSonarStatus[8];
UserData currentUser;

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
  
  // Play WiFi connecting voice (non-blocking so we can start connecting)
  voicePlayTrackNoWait(VOICE_WIFI_CONNECTING);
  
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
    voiceStop(); // Stop current track if it's still playing
    voicePlayTrack(VOICE_ERROR_WIFI_FAILED);  // Blocking error
    displaySerial.print(F("{\"type\":\"SYSTEM_STATUS\",\"message\":\"WiFi connection failed\",\"wifi_connected\":false,\"ip\":\"0.0.0.0\"}\n"));
  }
}

void ensureWiFiConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WiFi disconnected. Reconnecting..."));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    unsigned long reconnectTimeout = millis();
    while (millis() - reconnectTimeout < 15000) {
      if (WiFi.status() == WL_CONNECTED) break;
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
    // Wait for WiFi connecting voice to finish or near finish before playing Ready
    while (voiceIsPlaying()) delay(100); 
    voicePlayTrackNoWait(VOICE_SYSTEM_READY);
  }
  
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"type\":\"SYSTEM_STATUS\",\"message\":\"Firebase %s\",\"firebase_connected\":%s,\"ready\":%s}\n", 
           signupOK ? "connected" : "failed", signupOK ? "true" : "false", signupOK ? "true" : "false");
  displaySerial.print(buf);
}

UserData fetchUserData(String rfidNumber) {
  UserData user;
  memset(&user, 0, sizeof(UserData));
  user.isLoggedIn = false;
  rfidNumber.trim();
  strncpy(user.rfid, rfidNumber.c_str(), sizeof(user.rfid) - 1);
  
  UserRecord localUser;
  if (getUserByRFID(rfidNumber, localUser)) {
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
  if (Firebase.ready()) {
    String basePath = "USERS/" + rfidNumber;
    if (Firebase.RTDB.getString(&fbdo, basePath + "/name")) {
      strncpy(user.name, fbdo.stringData().c_str(), sizeof(user.name) - 1);
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
  return user;
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
  updateUserFingerprint(rfidNumber, fingerprintID);
  ensureWiFiConnected();
  if (Firebase.ready()) {
    String basePath = "USERS/" + rfidNumber;
    bool success = true;
    success &= Firebase.RTDB.setBool(&fbdo, basePath + "/fingerprint_registered", status);
    success &= Firebase.RTDB.setInt(&fbdo, basePath + "/fingerprint_id", fingerprintID);
    return success;
  }
  return true;
}

uint8_t getFingerprintIDFromFirebase(String rfidNumber) {
  UserRecord localUser;
  if (getUserByRFID(rfidNumber, localUser)) {
    if (localUser.fingerprint_registered) return localUser.fingerprint_id;
  }
  ensureWiFiConnected();
  if (Firebase.ready()) {
    String path = "USERS/" + rfidNumber + "/fingerprint_id";
    if (Firebase.RTDB.getInt(&fbdo, path)) return (uint8_t)fbdo.intData();
  }
  return 0;
}

uint8_t rfidToFingerprintID(String rfidNumber) {
  unsigned long rfidHash = 0x811C9DC5;
  for (int i = 0; i < rfidNumber.length(); i++) {
    rfidHash ^= (unsigned long)rfidNumber[i];
    rfidHash *= 0x01000193;
  }
  return (rfidHash % 127) + 1;
}

void enrollmentProcess() {
  ensureWiFiConnected();
  Serial.println("=== ENROLLMENT ===");
  voicePlayTrackNoWait(VOICE_ENROLL_SCAN_RFID);
  sendPrompt("Please scan your RFID card");
  
  tidString = "NIL";
  while (tidString == "NIL" && !cancelCurrentProcess) {
    readRFID();
    delay(100);
  }
  
  if (cancelCurrentProcess) {
    cancelCurrentProcess = false;
    return;
  }
  
  voicePlayTrackNoWait(VOICE_ENROLL_RFID_DETECTED);
  currentUser = fetchUserData(tidString);
  
  if (currentUser.name == "" || strcmp(currentUser.name, "Unknown") == 0) {
    voicePlayTrackNoWait(VOICE_ERROR_USER_NOT_FOUND);
    sendPrompt("ERROR: RFID not registered!");
    return;
  }
  
  uint8_t fingerprintID = rfidToFingerprintID(tidString);
  id = fingerprintID;
  
  if (getFingerprintEnroll()) {
    voicePlayTrackNoWait(VOICE_ENROLL_SUCCESS);
    sendPrompt("SUCCESS: Enrollment complete!");
    updateFingerprintStatus(tidString, true, fingerprintID);
    
    currentUser.isLoggedIn = true;
    previousState = currentState;
    currentState = STATE_DASHBOARD;
    delay(2000);
    voicePlayTrackNoWait(VOICE_DASHBOARD_INSTRUCTIONS);
    sendUserData();
  } else {
    voicePlayTrackNoWait(VOICE_ENROLL_FAILED);
    sendPrompt("FAILED: Enrollment unsuccessful");
  }
}

void loginProcess() {
  ensureWiFiConnected();
  Serial.println("=== LOGIN ===");
  voicePlayTrackNoWait(VOICE_LOGIN_SCAN_RFID);
  sendPrompt("Please scan your RFID card...");
  
  tidString = "NIL";
  while (tidString == "NIL" && !cancelCurrentProcess) {
    readRFID();
    delay(100);
  }
  
  if (cancelCurrentProcess) {
    cancelCurrentProcess = false;
    return;
  }
  
  voicePlayTrackNoWait(VOICE_LOGIN_RFID_DETECTED);
  currentUser = fetchUserData(tidString);
  
  if (currentUser.name == "" || strcmp(currentUser.name, "Unknown") == 0) {
    voicePlayTrackNoWait(VOICE_ERROR_USER_NOT_FOUND);
    sendPrompt("ERROR: User not found!");
    return;
  }
  
  voicePlayTrackNoWait(VOICE_LOGIN_FINGERPRINT_SCANNING);
  delay(1000);
  
  uint8_t expectedID = getFingerprintIDFromFirebase(tidString);
  if (expectedID == 0) {
    sendSimpleMessage("FINGERPRINT_ERROR", "No fingerprint registered.");
    return;
  }
  
  int fingerprintResult = getFingerprintIDez();
  if (fingerprintResult == expectedID) {
    currentUser.isLoggedIn = true;
    previousState = currentState;
    currentState = STATE_DASHBOARD;
    voicePlayTrackNoWait(VOICE_LOGIN_SUCCESS);
    sendSimpleMessage("FINGERPRINT_SUCCESS", "Verified");
    delay(2000);
    voicePlayTrackNoWait(VOICE_DASHBOARD_INSTRUCTIONS);
    sendUserData();
  } else {
    voicePlayTrackNoWait(VOICE_LOGIN_FAILED);
    sendSimpleMessage("FINGERPRINT_ERROR", "Verification failed");
  }
}

void writeFirebaseDB() {
  if (currentUser.rfid == "") return;
  
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
  
  saveHealthReading(reading);
  ensureWiFiConnected();
  
  if (Firebase.ready()) {
    String userPath = String("READINGS/") + currentUser.rfid + "/latest";
    String timestamp = String(time(nullptr));
    bool success = true;
    success &= Firebase.RTDB.setFloat(&fbdo, userPath + "/heart_rate", user_hr);
    success &= Firebase.RTDB.setFloat(&fbdo, userPath + "/spo2", user_sp02);
    success &= Firebase.RTDB.setFloat(&fbdo, userPath + "/temperature", user_tempo);
    success &= Firebase.RTDB.setFloat(&fbdo, userPath + "/weight", user_weight);
    success &= Firebase.RTDB.setFloat(&fbdo, userPath + "/height", user_height_laser);
    success &= Firebase.RTDB.setFloat(&fbdo, userPath + "/bmi_laser", user_bmi_laser);
    success &= Firebase.RTDB.setString(&fbdo, userPath + "/timestamp", timestamp);
    
    if (success) markReadingAsSynced(currentUser.rfid, reading.timestamp);
  }
}

void handleDisplayCommands() {
  if (!displaySerial.available()) return;
  
  static char cmdBuf[128];
  static uint8_t idx = 0;
  
  while (displaySerial.available()) {
    char c = displaySerial.read();
    if (c == '\n' || c == '\r') {
      if (idx > 0) {
        cmdBuf[idx] = '\0';
        idx = 0; // Reset for next command
        
        Serial.print("Command received: "); Serial.println(cmdBuf);
        
        if (strstr(cmdBuf, "DISPLAY_READY")) {
            if (WiFi.status() == WL_CONNECTED) {
              char statusBuf[128];
              snprintf(statusBuf, sizeof(statusBuf), "{\"type\":\"SYSTEM_STATUS\",\"message\":\"WiFi connected\",\"wifi_connected\":true,\"ip\":\"%s\"}\n", WiFi.localIP().toString().c_str());
              displaySerial.print(statusBuf);
            }
            if (signupOK) displaySerial.print(F("{\"type\":\"SYSTEM_STATUS\",\"message\":\"Firebase connected\",\"firebase_connected\":true,\"ready\":true}\n"));
        } else if (strstr(cmdBuf, "BACK")) {
            if (currentState == STATE_DASHBOARD) {
              currentState = STATE_IDLE;
              currentUser.isLoggedIn = false;
            } else if (currentState == STATE_LOGIN || currentState == STATE_ENROLLMENT) {
              cancelCurrentProcess = true;
              currentState = STATE_IDLE;
            }
        } else if (strcmp(cmdBuf, "START_LOGIN") == 0) {
            if (currentState == STATE_IDLE) {
              currentState = STATE_LOGIN;
              loginProcess();
            }
        } else if (strcmp(cmdBuf, "START_ENROLLMENT") == 0) {
            if (currentState == STATE_IDLE) {
              currentState = STATE_ENROLLMENT;
              enrollmentProcess();
            }
        } else if (strcmp(cmdBuf, "READ_OXIMETER") == 0) {
            voicePlayTrackNoWait(VOICE_MEASUREMENT_STEP_ON_SCALE);
            voicePlayTrackNoWait(VOICE_PLACE_FINGER_OXIMETER);
            readOximeter();
            readESPNowData();
            voicePlayTrackNoWait(VOICE_MEASUREMENTS_COMPLETE);
            sendSensorData();
        } else if (strncmp(cmdBuf, "SAVE_READINGS", 13) == 0) {
            writeFirebaseDB();
            voicePlayTrackNoWait(VOICE_SAVE_SUCCESS);
        } else if (strcmp(cmdBuf, "LOGOUT") == 0) {
            currentState = STATE_IDLE;
            currentUser.isLoggedIn = false;
            voicePlayTrackNoWait(VOICE_LOGOUT);
        }
      }
    } else if (idx < sizeof(cmdBuf) - 1) {
      cmdBuf[idx++] = c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_I2C, SCL_I2C);
  pinMode(BUZZER, OUTPUT);
  
  delay(1000);
  Serial.println("...Starting Medic-Bot");
  
  voiceInit();
  voicePlayTrack(VOICE_WELCOME);
  
  initSDCard();
  initDatabase();
  
  displaySerial.begin(DISPLAY_BAUD, SERIAL_8N1, DISPLAY_RX, DISPLAY_TX);
  delay(3000);
  
  initWiFi();
  initESPNow();
  initFingerprint();
  initRFID();
  initOximeter();
  initFirebase();
  
  // FLUSH SERIAL BUFFER before starting the loop to avoid stale/noisy commands
  while(displaySerial.available()) displaySerial.read();
  
  Serial.println("System ready");
}

void loop() {
  handleDisplayCommands();
  
  static unsigned long lastSyncCheck = 0;
  if (millis() - lastSyncCheck > 60000) {
    lastSyncCheck = millis();
    // sync logic here if needed
  }
  delay(100);
}
