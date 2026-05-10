#include "esp_now_module.h"
#include "config.h"

// Data structure matching HEIGHT_WEIGHT_MODULE
// CRITICAL: Must match exactly with HEIGHT_WEIGHT_MODULE.ino line 66
typedef struct {
  float weight_kg;
  float height_sonar_cm;
  float height_lidar_cm;
  float bmi_sonar;
  float bmi_lidar;
  uint32_t timestamp;
} __attribute__((packed)) measurement_data_t;

struct_message myData;
struct_message board1, board2, board3;
struct_message boardsStruct[3] = { board1, board2, board3 };

extern float user_weight, user_tempo, user_tempa, user_height_sonar, user_height_laser, motor_height;
extern float user_bmi_laser, user_bmi_sonar;
extern char weightStatus[8], tempaStatus[8], tempoStatus[8], heightLaserStatus[8], heightSonarStatus[8];
extern char bmiLaserStatus[8], bmiSonarStatus[8];

// MAC address of HEIGHT_WEIGHT_MODULE ESP32-S3
// TODO: Replace with actual MAC address from HEIGHT_WEIGHT_MODULE
uint8_t heightWeightModuleMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void initESPNow() {
  // WiFi is already initialized in initWiFi(), don't change mode again
  // WiFi.mode(WIFI_STA);  // REMOVED - causes conflict
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    Serial.println("Continuing without ESP-NOW...");
    return;  // Return instead of infinite loop
  }
  
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
  
  // Add HEIGHT_WEIGHT_MODULE as peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, heightWeightModuleMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add HEIGHT_WEIGHT_MODULE peer");
  }
  
  Serial.print("Main Controller MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("ESP-NOW initialized successfully");
}

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  char macStr[18];
  Serial.print("Packet received from: ");
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.println(macStr);
  
  // Check if it's measurement data from HEIGHT_WEIGHT_MODULE
  if (len == sizeof(measurement_data_t)) {
    measurement_data_t* data = (measurement_data_t*)incomingData;
    Serial.println("Received HEIGHT_WEIGHT data:");
    Serial.print("Weight: "); Serial.print(data->weight_kg); Serial.println(" kg");
    Serial.print("Height (Sonar): "); Serial.print(data->height_sonar_cm); Serial.println(" cm");
    Serial.print("Height (LiDAR): "); Serial.print(data->height_lidar_cm); Serial.println(" cm");
    Serial.print("BMI (Sonar): "); Serial.println(data->bmi_sonar);
    Serial.print("BMI (LiDAR): "); Serial.println(data->bmi_lidar);
    
    // Store in global variables - prefer LiDAR for accuracy
    user_weight = data->weight_kg;
    user_height_laser = data->height_lidar_cm / 100.0; // Convert cm to meters
    user_height_sonar = data->height_sonar_cm / 100.0; // Convert cm to meters
    user_bmi_laser = data->bmi_lidar;
    user_bmi_sonar = data->bmi_sonar;
  } else {
    // Legacy format for other boards
    memcpy(&myData, incomingData, sizeof(myData));
    Serial.printf("Board ID %u: %u bytes\n", myData.id, len);
    
    boardsStruct[myData.id - 1].a = myData.a;
    boardsStruct[myData.id - 1].b = myData.b;
    boardsStruct[myData.id - 1].c = myData.c;
  }
}

void requestESPNowData() {
  Serial.println("Requesting data from HEIGHT_WEIGHT_MODULE...");
  
  // Send request command (0xAA = request code)
  uint8_t requestCmd = 0xAA;
  esp_err_t result = esp_now_send(heightWeightModuleMAC, &requestCmd, 1);
  
  if (result == ESP_OK) {
    Serial.println("Request sent successfully");
  } else {
    Serial.println("Error sending request");
  }
  
  // Wait for response (data will arrive via OnDataRecv callback)
  delay(500);
}

void readESPNowData() {
  Serial.print("W-BODY = "); Serial.println(boardsStruct[0].a);
  Serial.print("W-TOLR = "); Serial.println(boardsStruct[0].b);
  Serial.print("W-STAT = "); Serial.println(boardsStruct[0].c);

  Serial.print("T-BODY = "); Serial.println(boardsStruct[2].a);
  Serial.print("T-AMBT = "); Serial.println(boardsStruct[2].b);
  Serial.print("B-DIST = "); Serial.println(boardsStruct[2].c);

  Serial.print("H-SONAR = "); Serial.println(boardsStruct[1].a);
  Serial.print("H-LASER = "); Serial.println(boardsStruct[1].b);
  Serial.print("H-MOTOR = "); Serial.println(boardsStruct[1].c);
  Serial.println();

  user_weight = (float)boardsStruct[0].a;
  user_tempo = (float)boardsStruct[1].a;
  user_tempa = (float)boardsStruct[1].b;
  user_height_sonar = HEIGHT_REF - (float)boardsStruct[2].a;
  user_height_laser = HEIGHT_REF - (float)boardsStruct[2].b;
  motor_height = (float)boardsStruct[2].c;
  
  user_bmi_laser = user_weight / (user_height_laser * user_height_laser);
  user_bmi_sonar = user_weight / (user_height_sonar * user_height_sonar);

  // Status mapping for height laser
  if (!isnan(user_height_laser)) {
    if (user_height_laser < 1.45) strcpy(heightLaserStatus, "DWARF");
    else if (user_height_laser < 1.65) strcpy(heightLaserStatus, "SHORT");
    else if (user_height_laser < 1.78) strcpy(heightLaserStatus, "AVG");
    else if (user_height_laser < 2.0) strcpy(heightLaserStatus, "TALL");
    else strcpy(heightLaserStatus, "GIGA");
  } else strcpy(heightLaserStatus, "NIL");

  // Status mapping for height sonar
  if (!isnan(user_height_sonar)) {
    if (user_height_sonar < 1.45) strcpy(heightSonarStatus, "DWARF");
    else if (user_height_sonar < 1.65) strcpy(heightSonarStatus, "SHORT");
    else if (user_height_sonar < 1.78) strcpy(heightSonarStatus, "AVG");
    else if (user_height_sonar < 2.0) strcpy(heightSonarStatus, "TALL");
    else strcpy(heightSonarStatus, "GIGA");
  } else strcpy(heightSonarStatus, "NIL");

  // Status mapping for weight
  if (!isnan(user_weight)) {
    if (user_weight < 50) strcpy(weightStatus, "UNDER");
    else if (user_weight < 70) strcpy(weightStatus, "NORM");
    else if (user_weight < 85) strcpy(weightStatus, "OVER");
    else if (user_weight < 120) strcpy(weightStatus, "OBES1");
    else strcpy(weightStatus, "OBES2");
  } else strcpy(weightStatus, "NIL");

  // Status mapping for BMI laser
  if (!isnan(user_bmi_laser)) {
    if (user_bmi_laser < 18.5) strcpy(bmiLaserStatus, "UNDER");
    else if (user_bmi_laser < 24.9) strcpy(bmiLaserStatus, "NORM");
    else if (user_bmi_laser < 30) strcpy(bmiLaserStatus, "OVER");
    else if (user_bmi_laser < 34.9) strcpy(bmiLaserStatus, "OBES1");
    else if (user_bmi_laser < 39.9) strcpy(bmiLaserStatus, "OBES2");
    else strcpy(bmiLaserStatus, "OBES3");
  } else strcpy(bmiLaserStatus, "NIL");

  // Status mapping for BMI sonar
  if (!isnan(user_bmi_sonar)) {
    if (user_bmi_sonar < 18.5) strcpy(bmiSonarStatus, "UNDER");
    else if (user_bmi_sonar < 24.9) strcpy(bmiSonarStatus, "NORM");
    else if (user_bmi_sonar < 30) strcpy(bmiSonarStatus, "OVER");
    else if (user_bmi_sonar < 34.9) strcpy(bmiSonarStatus, "OBES1");
    else if (user_bmi_sonar < 39.9) strcpy(bmiSonarStatus, "OBES2");
    else strcpy(bmiSonarStatus, "OBES3");
  } else strcpy(bmiSonarStatus, "NIL");

  // Status mapping for ambient temperature
  if (!isnan(user_tempa)) {
    if (user_tempa < 25) strcpy(tempaStatus, "LOW");
    else if (user_tempa < 30) strcpy(tempaStatus, "ROOM");
    else if (user_tempa < 37.5) strcpy(tempaStatus, "NORM");
    else if (user_tempa < 40) strcpy(tempaStatus, "HIGH");
    else strcpy(tempaStatus, "EXTR");
  } else strcpy(tempaStatus, "NIL");

  // Status mapping for body temperature
  if (!isnan(user_tempo)) {
    if (user_tempo < 28) strcpy(tempoStatus, "LHYP");
    else if (user_tempo < 32) strcpy(tempoStatus, "LOW");
    else if (user_tempo < 37.5) strcpy(tempoStatus, "NORM");
    else if (user_tempo < 40) strcpy(tempoStatus, "HIGH");
    else strcpy(tempoStatus, "HHYP");
  } else strcpy(tempoStatus, "NIL");
}