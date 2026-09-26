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
#include <VL53L0X.h>
#include <VL53L1X.h>
#include "height_pins.h"
#include "espnow_protocol.h"

static const char *TAG = "HEIGHT_NODE";

typedef enum {
    SENSOR_NONE = 0,
    SENSOR_POLOLU_L1X,
    SENSOR_POLOLU_L0X,
    SENSOR_ADA_L0X
} vl53_sensor_type_t;

// ===== GLOBAL SENSOR OBJECTS =====
static Adafruit_VL53L0X g_ada_vl53l0x = Adafruit_VL53L0X();
static VL53L0X g_pololu_vl53;
static VL53L1X g_pololu_vl53l1x;
static vl53_sensor_type_t g_sensor_type = SENSOR_NONE;

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
static float read_sonar_distance_cm(uint8_t pinA, uint8_t pinB) {
    // Attempt 1: Normal Pin Mapping (pinA = TRIG, pinB = ECHO)
    pinMode(pinA, OUTPUT);
    pinMode(pinB, INPUT);
    digitalWrite(pinA, LOW);
    delayMicroseconds(4);
    digitalWrite(pinA, HIGH);
    delayMicroseconds(10);
    digitalWrite(pinA, LOW);

    unsigned long duration_us = pulseIn(pinB, HIGH, 30000);
    if (duration_us > 0) {
        float dist_cm = (float)duration_us * 0.0343f / 2.0f;
        if (dist_cm >= MIN_MEASURABLE_DIST_CM && dist_cm <= MAX_MEASURABLE_DIST_CM) {
            return dist_cm;
        }
    }

    // Attempt 2: Swapped Pin Mapping (pinB = TRIG, pinA = ECHO)
    pinMode(pinB, OUTPUT);
    pinMode(pinA, INPUT);
    digitalWrite(pinB, LOW);
    delayMicroseconds(4);
    digitalWrite(pinB, HIGH);
    delayMicroseconds(10);
    digitalWrite(pinB, LOW);

    duration_us = pulseIn(pinA, HIGH, 30000);
    if (duration_us > 0) {
        float dist_cm = (float)duration_us * 0.0343f / 2.0f;
        if (dist_cm >= MIN_MEASURABLE_DIST_CM && dist_cm <= MAX_MEASURABLE_DIST_CM) {
            Serial.printf("[AUTO-DETECT] Pins %d & %d are physically SWAPPED! (Swapped Trig=%d, Echo=%d works!)\n", pinA, pinB, pinB, pinA);
            return dist_cm;
        }
    }

    // Reset back to original pin directions
    pinMode(pinA, OUTPUT);
    pinMode(pinB, INPUT);
    return -1.0f;
}

/* =========================================================================
 * Optical Laser Time-of-Flight (VL53L0X) Driver
 * ========================================================================= */
static float read_laser_distance_cm() {
    if (g_sensor_type == SENSOR_POLOLU_L1X) {
        uint16_t dist_mm = g_pololu_vl53l1x.read();
        if (!g_pololu_vl53l1x.timeoutOccurred()) {
            float dist_cm = (float)dist_mm / 10.0f;
            if (dist_cm >= MIN_MEASURABLE_DIST_CM && dist_cm <= MAX_MEASURABLE_DIST_CM) {
                return dist_cm;
            }
        }
    } else if (g_sensor_type == SENSOR_POLOLU_L0X) {
        uint16_t dist_mm = g_pololu_vl53.readRangeContinuousMillimeters();
        if (!g_pololu_vl53.timeoutOccurred()) {
            float dist_cm = (float)dist_mm / 10.0f;
            if (dist_cm >= MIN_MEASURABLE_DIST_CM && dist_cm <= MAX_MEASURABLE_DIST_CM) {
                return dist_cm;
            }
        }
    } else if (g_sensor_type == SENSOR_ADA_L0X) {
        VL53L0X_RangingMeasurementData_t measure;
        g_ada_vl53l0x.rangingTest(&measure, false);
        if (measure.RangeStatus != 4 && measure.RangeMilliMeter > 0) {
            float dist_cm = (float)measure.RangeMilliMeter / 10.0f;
            if (dist_cm >= MIN_MEASURABLE_DIST_CM && dist_cm <= MAX_MEASURABLE_DIST_CM) {
                return dist_cm;
            }
        }
        uint16_t dist_mm = g_ada_vl53l0x.readRangeResult();
        if (dist_mm > 0 && dist_mm < 8190) {
            float dist_cm = (float)dist_mm / 10.0f;
            if (dist_cm >= MIN_MEASURABLE_DIST_CM && dist_cm <= MAX_MEASURABLE_DIST_CM) {
                return dist_cm;
            }
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
static float measure_patient_height_cm(float &out_sonar_ht_cm, float &out_laser_ht_cm) {
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

        Serial.printf("[%s] Burst %d -> S1: %.1f | S2: %.1f | S3: %.1f | ToF: %.1f\n",
                      TAG, b + 1, s1_readings[b], s2_readings[b], s3_readings[b], tof_readings[b]);
    }

    // Calculate Sonar average height
    float sonar_sum = 0.0f;
    int sonar_count = 0;
    for (int b = 0; b < NUM_BURSTS; b++) {
        if (s1_readings[b] > 0.0f) { sonar_sum += s1_readings[b]; sonar_count++; }
        if (s2_readings[b] > 0.0f) { sonar_sum += s2_readings[b]; sonar_count++; }
        if (s3_readings[b] > 0.0f) { sonar_sum += s3_readings[b]; sonar_count++; }
    }
    float sonar_crown_dist = (sonar_count > 0) ? (sonar_sum / (float)sonar_count) : 0.0f;
    out_sonar_ht_cm = (sonar_crown_dist > 0.0f) ? (g_stand_height_cm - sonar_crown_dist) : 0.0f;

    // Calculate Laser average height
    float laser_sum = 0.0f;
    int laser_count = 0;
    for (int b = 0; b < NUM_BURSTS; b++) {
        if (tof_readings[b] > 0.0f) { laser_sum += tof_readings[b]; laser_count++; }
    }
    float laser_crown_dist = (laser_count > 0) ? (laser_sum / (float)laser_count) : 0.0f;
    out_laser_ht_cm = (laser_crown_dist > 0.0f) ? (g_stand_height_cm - laser_crown_dist) : out_sonar_ht_cm;

    float fused_ht = (out_sonar_ht_cm > 0.0f) ? out_sonar_ht_cm : out_laser_ht_cm;

    Serial.printf("[%s] Fusion Result -> Sonar Height: %.1f cm | Laser Height: %.1f cm | Stand: %.1f cm\n",
                  TAG, out_sonar_ht_cm, out_laser_ht_cm, g_stand_height_cm);

    return fused_ht;
}

static float measure_patient_height_cm() {
    float s = 0.0f, l = 0.0f;
    return measure_patient_height_cm(s, l);
}

/* =========================================================================
 * Master-Worker ESP-NOW Communication Protocol
 * ========================================================================= */
static void send_espnow_response(uint8_t opcode, float value, uint8_t status, float laser_val = 0.0f) {
    espnow_kiosk_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.magic = ESPNOW_MAGIC_BYTE;
    pkt.src_node = NODE_HEIGHT_SONAR;
    pkt.dest_node = NODE_MAIN_CONTROLLER;
    pkt.opcode = opcode;
    pkt.seq = ++g_packet_seq;
    pkt.status = status;

    if (opcode == RESP_HEIGHT) {
        pkt.data.dual_height.height_sonar_cm = value;
        pkt.data.dual_height.height_laser_cm = (laser_val > 0.0f) ? laser_val : value;
        pkt.data.height = value;
    } else if (opcode == RESP_GANTRY_POS) {
        pkt.data.gantry_pos.current_position_cm = value;
        pkt.data.gantry_pos.aligned = (status == 0 ? 1 : 0);
        pkt.data.height = value;
    } else {
        pkt.data.height = value;
    }

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

    // 2. Full I2C Bus Scan & Multi-Driver VL53 Laser Sensor Cascade
    Serial.println("\n--- FULL I2C BUS SCAN (SDA=21, SCL=22) ---");
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);

    bool found_29 = false;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  [I2C DEVICE FOUND] Active responder at 0x%02X\n", addr);
            if (addr == 0x29) found_29 = true;
        }
    }

    if (found_29) {
        delay(20);
        // Print key register values at 0x29 for diagnostic verification
        Serial.print("  [DIAGNOSTIC] Register Dump at 0x29: ");
        uint8_t regs[] = {0xC0, 0xC1, 0xC2, 0x51, 0x80, 0x00, 0xAA, 0xEE};
        for (int r = 0; r < 8; r++) {
            Wire.beginTransmission(0x29);
            Wire.write(regs[r]);
            Wire.endTransmission();
            Wire.requestFrom((uint8_t)0x29, (uint8_t)1);
            uint8_t val = Wire.available() ? Wire.read() : 0xFF;
            Serial.printf("Reg 0x%02X=0x%02X  ", regs[r], val);
        }
        Serial.println();

        // Driver Attempt 1: Pololu VL53L1X Driver (For VL53L1X hardware / vl53l0/1xv2 boards)
        g_pololu_vl53l1x.setBus(&Wire);
        if (g_pololu_vl53l1x.init()) {
            g_sensor_type = SENSOR_POLOLU_L1X;
            g_pololu_vl53l1x.setTimeout(500);
            g_pololu_vl53l1x.startContinuous(50);
            Serial.printf("[SETUP] >>> SUCCESS: Pololu VL53L1X Driver initialized on SDA=21, SCL=22! <<<\n");
        }
        // Driver Attempt 2: Adafruit VL53L0X Standard Driver
        else if (g_ada_vl53l0x.begin(VL53L0X_I2C_ADDR, false, &Wire)) {
            g_sensor_type = SENSOR_ADA_L0X;
            g_ada_vl53l0x.startRangeContinuous();
            Serial.printf("[SETUP] >>> SUCCESS: Adafruit VL53L0X Driver initialized on SDA=21, SCL=22! <<<\n");
        }
        // Driver Attempt 3: Pololu VL53L0X (2V8 I/O mode true)
        else {
            g_pololu_vl53.setBus(&Wire);
            if (g_pololu_vl53.init(true)) {
                g_sensor_type = SENSOR_POLOLU_L0X;
                g_pololu_vl53.setTimeout(500);
                g_pololu_vl53.startContinuous(33);
                Serial.printf("[SETUP] >>> SUCCESS: Pololu VL53L0X (2V8 mode) initialized on SDA=21, SCL=22! <<<\n");
            }
            // Driver Attempt 4: Pololu VL53L0X (1V8 I/O mode false)
            else if (g_pololu_vl53.init(false)) {
                g_sensor_type = SENSOR_POLOLU_L0X;
                g_pololu_vl53.setTimeout(500);
                g_pololu_vl53.startContinuous(33);
                Serial.printf("[SETUP] >>> SUCCESS: Pololu VL53L0X (1V8 mode) initialized on SDA=21, SCL=22! <<<\n");
            } else {
                g_sensor_type = SENSOR_NONE;
                Serial.println("[SETUP] Warning: All VL53 drivers (L0X & L1X) failed to initialize despite address 0x29 ACK.");
            }
        }
    } else {
        g_sensor_type = SENSOR_NONE;
        Serial.println("[SETUP] Notice: No I2C device detected at address 0x29.");
    }

    // 3. Initialize ESP-NOW
    init_espnow();

    Serial.println("[SETUP] Height Subsystem Ready. Type 'm' in Serial for test measurement, 'g' for gantry position.");
}

void loop() {
    // 1. Handle ESP-NOW Triggered Patient Height Measurement
    if (g_trigger_patient_measurement) {
        g_trigger_patient_measurement = false;

        float sonar_ht = 0.0f, laser_ht = 0.0f;
        measure_patient_height_cm(sonar_ht, laser_ht);
        uint8_t status = (sonar_ht > 0.0f || laser_ht > 0.0f) ? 0 : 1; // 0 = OK, 1 = Error
        send_espnow_response(RESP_HEIGHT, sonar_ht, status, laser_ht);
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
