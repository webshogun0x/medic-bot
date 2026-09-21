/*
 * MediBot Health Kiosk - Height Subsystem Firmware
 * Hardware: ESP-WROOM-32
 * Sensors:
 *   - VL53L0X Time-of-Flight / Laser Distance Sensor (I2C: SDA=21, SCL=22)
 *   - 3x Patient Height Sonars for Spatial Triangulation (Pins: 12/13, 14/27, 26/25)
 *   - 1x Gantry Carriage Tracker Sonar (Pins: 33/32)
 *
 * Communication:
 *   - Master-Worker 2.4 GHz ESP-NOW (Slave node: NODE_HEIGHT_SONAR)
 *
 * Authors: Idowu Oluwatimileyin (The_Shogunate) & Engineering Team
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include "height_pins.h"
#include "espnow_protocol.h"

static const char *TAG = "HEIGHT_NODE";

// ===== GLOBAL SENSOR OBJECTS =====
Adafruit_VL53L0X g_vl53 = Adafruit_VL53L0X();
static bool g_vl53_detected = false;

// Configurable stand geometry (Overhead mount height from platform)
static float g_stand_height_cm = DEFAULT_STAND_HEIGHT_CM;

// Broadcast MAC (or locked to Main Controller)
static uint8_t g_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint16_t g_packet_seq = 0;

// Measurement trigger flags
static volatile bool g_trigger_patient_measurement = false;
static volatile bool g_trigger_gantry_measurement = false;
static volatile bool g_channel_ack_received = false;
static uint8_t g_active_channel = 1;

/* =========================================================================
 * Low-Level Ultrasonic Pulse Timing Driver
 * ========================================================================= */
static float read_sonar_distance_cm(uint8_t trigPin, uint8_t echoPin) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    // Wait for Echo to go HIGH (5ms timeout)
    uint32_t start_wait = micros();
    while (digitalRead(echoPin) == LOW) {
        if (micros() - start_wait > 5000) {
            return -1.0f; // Timeout waiting for pulse start
        }
    }

    // Measure pulse width while Echo is HIGH (30ms timeout ~ 5 meters)
    uint32_t pulse_start = micros();
    while (digitalRead(echoPin) == HIGH) {
        if (micros() - pulse_start > 30000) {
            return -1.0f; // Echo pulse timeout
        }
    }
    uint32_t pulse_end = micros();

    uint32_t duration_us = pulse_end - pulse_start;
    float distance_cm = (float)duration_us * 0.0343f / 2.0f;

    if (distance_cm >= MIN_MEASURABLE_DIST_CM && distance_cm <= MAX_MEASURABLE_DIST_CM) {
        return distance_cm;
    }
    return -1.0f;
}

/* =========================================================================
 * Optical Laser Time-of-Flight (VL53L0X) Driver
 * ========================================================================= */
static float read_laser_distance_cm() {
    if (!g_vl53_detected) {
        return -1.0f;
    }

    VL53L0X_RangingMeasurementData_t measure;
    g_vl53.rangingTest(&measure, false);

    if (measure.RangeStatus != 4) { // 4 = out of range
        float dist_cm = (float)measure.RangeMilliMeter / 10.0f;
        if (dist_cm >= MIN_MEASURABLE_DIST_CM && dist_cm <= MAX_MEASURABLE_DIST_CM) {
            return dist_cm;
        }
    }
    return -1.0f;
}

/* =========================================================================
 * Gantry Carriage Position Measurement (Sonar 4)
 * ========================================================================= */
static float measure_gantry_height_cm() {
    float sum = 0.0f;
    int valid = 0;

    for (int i = 0; i < 3; i++) {
        float d = read_sonar_distance_cm(PIN_GANTRY_TRIG, PIN_GANTRY_ECHO);
        if (d > 0.0f) {
            sum += d;
            valid++;
        }
        delay(ACOUSTIC_STAGGER_MS);
    }

    if (valid > 0) {
        float avg_dist = sum / (float)valid;
        Serial.printf("[%s] Gantry Sonar: %.1f cm\n", TAG, avg_dist);
        return avg_dist;
    }
    return -1.0f;
}

/* =========================================================================
 * Multi-Sensor Fusion Engine for Patient Standing Height
 * Fuses 3-Point Ultrasonic Array + Center Optical Laser ToF
 * ========================================================================= */
static float measure_patient_height_cm() {
    Serial.printf("[%s] Starting Multi-Sensor Height Measurement...\n", TAG);

    const int NUM_BURSTS = 4;
    float s1_readings[NUM_BURSTS];
    float s2_readings[NUM_BURSTS];
    float s3_readings[NUM_BURSTS];
    float tof_readings[NUM_BURSTS];

    // Collect staggered spatial readings across bursts
    for (int b = 0; b < NUM_BURSTS; b++) {
        // Sonar 1 (Left)
        s1_readings[b] = read_sonar_distance_cm(PIN_SONAR1_TRIG, PIN_SONAR1_ECHO);
        delay(ACOUSTIC_STAGGER_MS);

        // Sonar 2 (Center)
        s2_readings[b] = read_sonar_distance_cm(PIN_SONAR2_TRIG, PIN_SONAR2_ECHO);
        delay(ACOUSTIC_STAGGER_MS);

        // Sonar 3 (Right)
        s3_readings[b] = read_sonar_distance_cm(PIN_SONAR3_TRIG, PIN_SONAR3_ECHO);
        delay(ACOUSTIC_STAGGER_MS);

        // Optical Laser ToF (Center Vertex)
        tof_readings[b] = read_laser_distance_cm();
        delay(ACOUSTIC_STAGGER_MS);
    }

    // Identify candidate minimum distances (crown of head is closest to overhead mount)
    float valid_distances[NUM_BURSTS * 4];
    int valid_count = 0;

    for (int b = 0; b < NUM_BURSTS; b++) {
        if (s1_readings[b] > 0.0f) valid_distances[valid_count++] = s1_readings[b];
        if (s2_readings[b] > 0.0f) valid_distances[valid_count++] = s2_readings[b];
        if (s3_readings[b] > 0.0f) valid_distances[valid_count++] = s3_readings[b];
        if (tof_readings[b] > 0.0f) valid_distances[valid_count++] = tof_readings[b];
    }

    if (valid_count < 3) {
        Serial.printf("[%s] Error: Insufficient valid distance echoes detected (%d readings)\n", TAG, valid_count);
        return 0.0f;
    }

    // Simple Bubble Sort to find median and lowest cluster
    for (int i = 0; i < valid_count - 1; i++) {
        for (int j = 0; j < valid_count - i - 1; j++) {
            if (valid_distances[j] > valid_distances[j + 1]) {
                float tmp = valid_distances[j];
                valid_distances[j] = valid_distances[j + 1];
                valid_distances[j + 1] = tmp;
            }
        }
    }

    // Discard upper quartile (floor/empty platform echoes) and average top 3 closest readings
    int sample_size = (valid_count >= 5) ? 3 : valid_count;
    float sum_head_dist = 0.0f;
    for (int i = 0; i < sample_size; i++) {
        sum_head_dist += valid_distances[i];
    }
    float crown_dist_cm = sum_head_dist / (float)sample_size;

    // Calculate standing height from stand ceiling geometry
    float patient_height_cm = g_stand_height_cm - crown_dist_cm;

    Serial.printf("[%s] Fusion Result -> Crown Distance: %.1f cm | Stand: %.1f cm | Calculated Height: %.1f cm\n",
                  TAG, crown_dist_cm, g_stand_height_cm, patient_height_cm);

    // Sanity boundary validation
    if (patient_height_cm < MIN_VALID_PATIENT_HT_CM || patient_height_cm > MAX_VALID_PATIENT_HT_CM) {
        Serial.printf("[%s] Warning: Calculated height %.1f cm outside clinical limits (%.0f - %.0f cm)\n",
                      TAG, patient_height_cm, MIN_VALID_PATIENT_HT_CM, MAX_VALID_PATIENT_HT_CM);
        return 0.0f;
    }

    return patient_height_cm;
}

/* =========================================================================
 * Master-Worker ESP-NOW Communication Protocol
 * ========================================================================= */
static void send_espnow_response(uint8_t opcode, float value, uint8_t status) {
    espnow_kiosk_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.magic = ESPNOW_MAGIC_BYTE;
    pkt.src_node = NODE_HEIGHT_SONAR;
    pkt.dest_node = NODE_MAIN_CONTROLLER;
    pkt.opcode = opcode;
    pkt.seq = ++g_packet_seq;
    pkt.status = status;
    pkt.data.height = value;

    esp_err_t result = esp_now_send(g_broadcast_mac, (uint8_t *)&pkt, sizeof(pkt));
    if (result == ESP_OK) {
        Serial.printf("[%s] Sent ESP-NOW Response (Opcode 0x%02X, Value: %.1f, Status: %d)\n", TAG, opcode, value, status);
    } else {
        Serial.printf("[%s] Error: Failed to transmit ESP-NOW packet (%d)\n", TAG, result);
    }
}

static void on_espnow_data_recv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len != sizeof(espnow_kiosk_packet_t)) {
        return; // Reject malformed packets
    }

    const espnow_kiosk_packet_t *pkt = (const espnow_kiosk_packet_t *)data;

    // Check magic byte
    if (pkt->magic != ESPNOW_MAGIC_BYTE) {
        return;
    }

    // Verify packet is destined for this node or broadcast
    if (pkt->dest_node != NODE_HEIGHT_SONAR && pkt->dest_node != 0xFF) {
        return;
    }

    // Any valid packet from Main Controller confirms channel lock
    if (pkt->src_node == NODE_MAIN_CONTROLLER) {
        g_channel_ack_received = true;
    }

    switch (pkt->opcode) {
        case CMD_GET_HEIGHT:
            g_trigger_patient_measurement = true;
            break;

        case CMD_GET_GANTRY_POS:
            g_trigger_gantry_measurement = true;
            break;

        case CMD_PING:
            send_espnow_response(RESP_HEIGHT, 0.0f, 0); // ACK
            break;

        default:
            break;
    }
}

static void auto_channel_hunt() {
    Serial.printf("[%s] Starting Auto-Channel Hunt (Scanning 2.4 GHz channels 1-13)...\n", TAG);
    for (uint8_t ch = 1; ch <= 13; ch++) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        g_channel_ack_received = false;

        // Send discovery ping to Main Controller
        send_espnow_response(CMD_PING, 0.0f, 0);

        uint32_t start_ms = millis();
        while (millis() - start_ms < 45) {
            if (g_channel_ack_received) {
                g_active_channel = ch;
                Serial.printf("[%s] >>> SUCCESS: Locked to Main Controller on Channel %d! <<<\n", TAG, ch);
                return;
            }
            delay(5);
        }
    }
    Serial.printf("[%s] Notice: Main Controller not heard during sweep. Defaulting to Channel %d.\n", TAG, g_active_channel);
    esp_wifi_set_channel(g_active_channel, WIFI_SECOND_CHAN_NONE);
}

static void init_espnow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    Serial.print("[");
    Serial.print(TAG);
    Serial.print("] Wi-Fi STA MAC: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.printf("[%s] Error initializing ESP-NOW\n", TAG);
        return;
    }

    esp_now_register_recv_cb(on_espnow_data_recv);

    // Register broadcast peer
    esp_now_peer_info_t peer_info;
    memset(&peer_info, 0, sizeof(peer_info));
    memcpy(peer_info.peer_addr, g_broadcast_mac, 6);
    peer_info.channel = 0;
    peer_info.encrypt = false;

    if (esp_now_add_peer(&peer_info) != ESP_OK) {
        Serial.printf("[%s] Error adding ESP-NOW broadcast peer\n", TAG);
    } else {
        Serial.printf("[%s] ESP-NOW initialized successfully (Master-Worker Slave Ready)\n", TAG);
    }

    auto_channel_hunt();
}

/* =========================================================================
 * Arduino Setup & Main Loop
 * ========================================================================= */
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n==================================================");
    Serial.println("   MediBot Health Kiosk - Height Subsystem");
    Serial.println("   ESP-WROOM-32 Multi-Sensor Fusion Engine");
    Serial.println("==================================================");

    // 1. Configure Ultrasonic GPIO Pins
    pinMode(PIN_SONAR1_TRIG, OUTPUT);
    pinMode(PIN_SONAR1_ECHO, INPUT);
    digitalWrite(PIN_SONAR1_TRIG, LOW);

    pinMode(PIN_SONAR2_TRIG, OUTPUT);
    pinMode(PIN_SONAR2_ECHO, INPUT);
    digitalWrite(PIN_SONAR2_TRIG, LOW);

    pinMode(PIN_SONAR3_TRIG, OUTPUT);
    pinMode(PIN_SONAR3_ECHO, INPUT);
    digitalWrite(PIN_SONAR3_TRIG, LOW);

    pinMode(PIN_GANTRY_TRIG, OUTPUT);
    pinMode(PIN_GANTRY_ECHO, INPUT);
    digitalWrite(PIN_GANTRY_TRIG, LOW);

    Serial.println("[SETUP] Configured 4 Ultrasonic Sensor GPIOs:");
    Serial.printf("  - Sonar 1 (Patient Left):   Trig=%d, Echo=%d\n", PIN_SONAR1_TRIG, PIN_SONAR1_ECHO);
    Serial.printf("  - Sonar 2 (Patient Center): Trig=%d, Echo=%d\n", PIN_SONAR2_TRIG, PIN_SONAR2_ECHO);
    Serial.printf("  - Sonar 3 (Patient Right):  Trig=%d, Echo=%d\n", PIN_SONAR3_TRIG, PIN_SONAR3_ECHO);
    Serial.printf("  - Sonar 4 (Gantry Tracker): Trig=%d, Echo=%d\n", PIN_GANTRY_TRIG, PIN_GANTRY_ECHO);

    // 2. Initialize I2C Bus & VL53L0X Laser Sensor
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000); // 400kHz Fast I2C

    if (g_vl53.begin()) {
        g_vl53_detected = true;
        Serial.printf("[SETUP] VL53L0X Laser ToF Sensor initialized on SDA=%d, SCL=%d\n", PIN_I2C_SDA, PIN_I2C_SCL);
    } else {
        g_vl53_detected = false;
        Serial.printf("[SETUP] Warning: VL53L0X not found on I2C (SDA=%d, SCL=%d). Continuing with 3x ultrasonic array.\n",
                      PIN_I2C_SDA, PIN_I2C_SCL);
    }

    // 3. Initialize ESP-NOW
    init_espnow();

    Serial.println("[SETUP] Height Subsystem Ready. Type 'm' in Serial for test measurement, 'g' for gantry position.");
}

void loop() {
    // 1. Handle ESP-NOW Triggered Patient Height Measurement
    if (g_trigger_patient_measurement) {
        g_trigger_patient_measurement = false;

        float ht = measure_patient_height_cm();
        uint8_t status = (ht > 0.0f) ? 0 : 1; // 0 = OK, 1 = Error
        send_espnow_response(RESP_HEIGHT, ht, status);
    }

    // 2. Handle ESP-NOW Triggered Gantry Position Measurement
    if (g_trigger_gantry_measurement) {
        g_trigger_gantry_measurement = false;

        float gantry_pos = measure_gantry_height_cm();
        uint8_t status = (gantry_pos > 0.0f) ? 0 : 1;
        send_espnow_response(RESP_GANTRY_POS, gantry_pos, status);
    }

    // 3. Serial Diagnostics & Manual Commands
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        if (cmd == 'm' || cmd == 'h') {
            Serial.println("\n--- MANUAL TEST: Patient Height Measurement ---");
            float ht = measure_patient_height_cm();
            Serial.printf(">>> Result: %.1f cm\n\n", ht);
        } else if (cmd == 'g') {
            Serial.println("\n--- MANUAL TEST: Gantry Carriage Height ---");
            float gp = measure_gantry_height_cm();
            Serial.printf(">>> Gantry Position: %.1f cm\n\n", gp);
        } else if (cmd == 't') {
            Serial.println("\n--- RAW SENSOR SWEEP ---");
            float s1 = read_sonar_distance_cm(PIN_SONAR1_TRIG, PIN_SONAR1_ECHO);
            delay(ACOUSTIC_STAGGER_MS);
            float s2 = read_sonar_distance_cm(PIN_SONAR2_TRIG, PIN_SONAR2_ECHO);
            delay(ACOUSTIC_STAGGER_MS);
            float s3 = read_sonar_distance_cm(PIN_SONAR3_TRIG, PIN_SONAR3_ECHO);
            delay(ACOUSTIC_STAGGER_MS);
            float s4 = read_sonar_distance_cm(PIN_GANTRY_TRIG, PIN_GANTRY_ECHO);
            float tof = read_laser_distance_cm();

            Serial.printf("Sonar 1 (L):  %.1f cm\n", s1);
            Serial.printf("Sonar 2 (C):  %.1f cm\n", s2);
            Serial.printf("Sonar 3 (R):  %.1f cm\n", s3);
            Serial.printf("Sonar 4 (G):  %.1f cm\n", s4);
            Serial.printf("Laser ToF:    %.1f cm\n\n", tof);
        }
    }

    delay(20);
}
