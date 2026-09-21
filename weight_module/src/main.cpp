/*
 * MediBot Health Kiosk - Weight Scale Subsystem Firmware
 * Target Microcontroller: ESP-12F / ESP-12E (ESP8266)
 * Hardware:
 *   - 4x Strain Gauge Load Cells in Wheatstone Bridge Configuration
 *   - HX711 24-bit ADC Amplifier (DOUT=GPIO4/D2, SCK=GPIO5/D1)
 *   - Master-Worker 2.4 GHz ESP-NOW (Slave node: NODE_WEIGHT_SCALE)
 *
 * Authors: Idowu Oluwatimileyin (The_Shogunate) & Engineering Team
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include "HX711.h"
#include "weight_pins.h"
#include "espnow_protocol.h"

static const char *TAG = "WEIGHT_NODE";

extern "C" {
  #include "user_interface.h"
}

// ===== GLOBAL OBJECTS & STATE =====
static HX711 scale;
static float g_calibration_factor = DEFAULT_CALIBRATION_FACTOR;
static uint8_t g_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint16_t g_packet_seq = 0;
static volatile bool g_trigger_measurement = false;
static volatile bool g_channel_ack_received = false;
static uint8_t g_active_channel = 1;

/* =========================================================================
 * HX711 Precision Driver & Stability Algorithm
 * ========================================================================= */
static void init_scale() {
    Serial.printf("[%s] Initializing HX711 on DOUT=GPIO%d, SCK=GPIO%d...\n", TAG, PIN_HX711_DOUT, PIN_HX711_SCK);
    scale.begin(PIN_HX711_DOUT, PIN_HX711_SCK);

    uint32_t start_wait = millis();
    while (!scale.is_ready()) {
        delay(50);
        if (millis() - start_wait > 3000) {
            Serial.printf("[%s] Warning: HX711 not responding! Check wiring and power.\n", TAG);
            break;
        }
    }

    if (scale.is_ready()) {
        scale.set_scale(g_calibration_factor);
        Serial.printf("[%s] HX711 Ready. Taring platform (10 samples)...\n", TAG);
        scale.tare(10);
        Serial.printf("[%s] Tare complete. Offset: %ld\n", TAG, scale.get_offset());
    }
}

static float measure_stable_weight_kg() {
    if (!scale.is_ready()) {
        Serial.printf("[%s] Error: Scale ADC not ready\n", TAG);
        return 0.0f;
    }

    Serial.printf("[%s] Starting Stable Weight Measurement...\n", TAG);

    // Turn on status LED during measurement (Active LOW)
    digitalWrite(PIN_STATUS_LED, LOW);

    const int MAX_ATTEMPTS = 15;
    const int SAMPLES_PER_READ = 3;
    float prev_weight = 0.0f;
    float stable_weight = 0.0f;
    int stable_streak = 0;

    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        float current = scale.get_units(SAMPLES_PER_READ);
        if (current < 0.0f) current = 0.0f;

        Serial.printf("[%s] Sample %d: %.2f kg\n", TAG, attempt + 1, current);

        if (attempt > 0) {
            float delta = fabs(current - prev_weight);
            if (delta <= STABILITY_DELTA_KG) {
                stable_streak++;
                if (stable_streak >= 2) {
                    stable_weight = (current + prev_weight) / 2.0f;
                    Serial.printf("[%s] Stable weight detected: %.2f kg (Streak=%d)\n", TAG, stable_weight, stable_streak);
                    break;
                }
            } else {
                stable_streak = 0;
            }
        }

        prev_weight = current;
        delay(120);
    }

    // Fallback if streak didn't lock: take a final 5-sample average
    if (stable_weight == 0.0f && prev_weight > 0.0f) {
        stable_weight = scale.get_units(5);
        if (stable_weight < 0.0f) stable_weight = 0.0f;
        Serial.printf("[%s] Final fallback average: %.2f kg\n", TAG, stable_weight);
    }

    // Turn off status LED
    digitalWrite(PIN_STATUS_LED, HIGH);

    return stable_weight;
}

/* =========================================================================
 * Master-Worker ESP-NOW Protocol (ESP8266 Implementation)
 * ========================================================================= */
static void send_espnow_response(uint8_t opcode, float value, uint8_t status) {
    espnow_kiosk_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.magic = ESPNOW_MAGIC_BYTE;
    pkt.src_node = NODE_WEIGHT_SCALE;
    pkt.dest_node = NODE_MAIN_CONTROLLER;
    pkt.opcode = opcode;
    pkt.seq = ++g_packet_seq;
    pkt.status = status;
    pkt.data.weight = value;

    int result = esp_now_send(g_broadcast_mac, (uint8_t *)&pkt, sizeof(pkt));
    if (result == 0) {
        Serial.printf("[%s] Sent ESP-NOW Weight Packet (Opcode 0x%02X, Weight: %.2f kg, Status: %d)\n",
                      TAG, opcode, value, status);
    } else {
        Serial.printf("[%s] Error: Failed to transmit ESP-NOW packet (%d)\n", TAG, result);
    }
}

static void on_espnow_data_recv(uint8_t *mac, uint8_t *data, uint8_t len) {
    if (len != sizeof(espnow_kiosk_packet_t)) {
        return; // Ignore invalid length
    }

    const espnow_kiosk_packet_t *pkt = (const espnow_kiosk_packet_t *)data;

    // Verify magic byte
    if (pkt->magic != ESPNOW_MAGIC_BYTE) {
        return;
    }

    // Check destination
    if (pkt->dest_node != NODE_WEIGHT_SCALE && pkt->dest_node != 0xFF) {
        return;
    }

    // Any valid packet from Main Controller confirms channel lock
    if (pkt->src_node == NODE_MAIN_CONTROLLER) {
        g_channel_ack_received = true;
    }

    switch (pkt->opcode) {
        case CMD_GET_WEIGHT:
            g_trigger_measurement = true;
            break;

        case CMD_PING:
            send_espnow_response(RESP_WEIGHT, 0.0f, 0); // ACK
            break;

        default:
            break;
    }
}

static void auto_channel_hunt() {
    Serial.printf("[%s] Starting Auto-Channel Hunt (Scanning 2.4 GHz channels 1-13)...\n", TAG);
    for (uint8_t ch = 1; ch <= 13; ch++) {
        wifi_set_channel(ch);
        esp_now_del_peer(g_broadcast_mac);
        esp_now_add_peer(g_broadcast_mac, ESP_NOW_ROLE_COMBO, ch, NULL, 0);

        g_channel_ack_received = false;
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
    wifi_set_channel(g_active_channel);
    esp_now_del_peer(g_broadcast_mac);
    esp_now_add_peer(g_broadcast_mac, ESP_NOW_ROLE_COMBO, g_active_channel, NULL, 0);
}

static void init_espnow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    Serial.print("[");
    Serial.print(TAG);
    Serial.print("] Wi-Fi STA MAC: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != 0) {
        Serial.printf("[%s] Error initializing ESP-NOW\n", TAG);
        return;
    }

    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(on_espnow_data_recv);

    // Register broadcast peer
    int res = esp_now_add_peer(g_broadcast_mac, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
    if (res != 0) {
        Serial.printf("[%s] Error adding broadcast peer (%d)\n", TAG, res);
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
    Serial.println("   MediBot Health Kiosk - Weight Scale Subsystem");
    Serial.println("   ESP-12F Precision HX711 Load Cell Node");
    Serial.println("==================================================");

    // Configure status LED
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH); // Off (Active LOW)

    // Initialize Scale & ESP-NOW
    init_scale();
    init_espnow();

    Serial.println("[SETUP] Weight Subsystem Ready. Type 'm' in Serial to trigger test measurement, 't' to tare.");
}

void loop() {
    // 1. Handle ESP-NOW Triggered Weight Measurement
    if (g_trigger_measurement) {
        g_trigger_measurement = false;

        float weight = measure_stable_weight_kg();
        uint8_t status = (weight > 0.0f) ? 0 : 1;
        send_espnow_response(RESP_WEIGHT, weight, status);
    }

    // 2. Serial Diagnostics & Manual Commands
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        if (cmd == 'm' || cmd == 'w') {
            Serial.println("\n--- MANUAL TEST: Stable Weight Measurement ---");
            float w = measure_stable_weight_kg();
            Serial.printf(">>> Result: %.2f kg\n\n", w);
        } else if (cmd == 't') {
            Serial.println("\n--- MANUAL TARE ---");
            scale.tare(10);
            Serial.printf(">>> Platform Tared. New Offset: %ld\n\n", scale.get_offset());
        } else if (cmd == 'r') {
            Serial.printf(">>> Raw ADC: %ld | Units: %.2f kg\n", scale.read(), scale.get_units(1));
        }
    }

    delay(20);
}
