/*
 * MediBot Health Kiosk - Weight Scale Subsystem Firmware (Quiet Sampling Node)
 * Target Microcontroller: ESP-12F / ESP-12E (ESP8266)
 * Hardware:
 *   - 4x Strain Gauge Load Cells in Wheatstone Bridge Configuration
 *   - HX711 24-bit ADC Amplifier (DOUT=GPIO4/D2, SCK=GPIO5/D1)
 *   - Master-Worker 2.4 GHz ESP-NOW (Slave node: NODE_WEIGHT_SCALE)
 *
 * Features:
 *   1. Dual-Trigger: Orchestrated (Kiosk CMD_GET_WEIGHT) + Autonomous (Step-On >= 15kg).
 *   2. Quiet Sampling Phase: Wi-Fi radio & ESP-NOW powered OFF during ADC sampling (pure DC, ~15mA, zero RF noise).
 *   3. Burst Transmission: Radio wakes up only after stable lock, bursts 3x RESP_WEIGHT packets, and returns to sleep.
 *   4. Interactive Calibration: 'c <kg>' on-the-fly calibration + fine-tuning via Serial.
 *
 * Authors: Idowu Oluwatimileyin (The_Shogunate) & Engineering Team
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include "HX711.h"
#include "weight_pins.h"
#include "espnow_protocol.h"

extern "C" {
  #include "user_interface.h"
}

static const char *TAG = "WEIGHT_NODE";

// Hardware Objects
static HX711 scale;
static float g_calibration_factor = DEFAULT_CALIBRATION_FACTOR;
static uint8_t g_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint16_t g_packet_seq = 0;
static uint8_t g_active_channel = 1;

static volatile bool g_orch_trigger = false;
static volatile bool g_channel_ack_received = false;
static bool g_radio_is_on = false;

/* =========================================================================
 * Radio Power Management (Quiet Sampling Engine)
 * ========================================================================= */
static void on_espnow_data_recv(uint8_t *mac, uint8_t *data, uint8_t len);

static void power_on_radio() {
    if (g_radio_is_on) return;

    Serial.printf("[%s] [POWER] Waking up Wi-Fi radio & ESP-NOW...\n", TAG);
    WiFi.forceSleepWake();
    delay(20);

    WiFi.mode(WIFI_STA);
    wifi_set_channel(g_active_channel);

    if (esp_now_init() == 0) {
        esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
        esp_now_register_recv_cb(on_espnow_data_recv);
        esp_now_add_peer(g_broadcast_mac, ESP_NOW_ROLE_COMBO, g_active_channel, NULL, 0);
        g_radio_is_on = true;
        Serial.printf("[%s] [POWER] Wi-Fi Active on Channel %d\n", TAG, g_active_channel);
    } else {
        Serial.printf("[%s] [POWER] Error re-initializing ESP-NOW\n", TAG);
    }
}

static void power_off_radio() {
    if (!g_radio_is_on) return;

    Serial.printf("[%s] [POWER] Entering Quiet Sampling Mode (Wi-Fi radio powering OFF)...\n", TAG);
    esp_now_deinit();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    delay(50);
    g_radio_is_on = false;
    Serial.printf("[%s] [POWER] ✅ Radio is OFF. Pure DC power & zero RF noise active.\n", TAG);
}

/* =========================================================================
 * ESP-NOW Protocol Callbacks & Helpers
 * ========================================================================= */
static void send_espnow_response(uint8_t opcode, float value, uint8_t status) {
    if (!g_radio_is_on) {
        power_on_radio();
    }

    espnow_kiosk_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.magic = ESPNOW_MAGIC_BYTE;
    pkt.src_node = NODE_WEIGHT_SCALE;
    pkt.dest_node = NODE_MAIN_CONTROLLER;
    pkt.opcode = opcode;
    pkt.seq = ++g_packet_seq;
    pkt.status = status;
    pkt.data.weight = value;

    // Send 3 burst transmissions to guarantee delivery
    for (int i = 0; i < 3; i++) {
        int res = esp_now_send(g_broadcast_mac, (uint8_t *)&pkt, sizeof(pkt));
        if (res == 0) {
            Serial.printf("[%s] Sent ESP-NOW Weight Packet #%d (Weight: %.2f kg, Status: %d)\n",
                          TAG, i + 1, value, status);
        }
        delay(15);
    }
}

static void on_espnow_data_recv(uint8_t *mac, uint8_t *data, uint8_t len) {
    if (len != sizeof(espnow_kiosk_packet_t)) return;

    const espnow_kiosk_packet_t *pkt = (const espnow_kiosk_packet_t *)data;
    if (pkt->magic != ESPNOW_MAGIC_BYTE) return;

    if (pkt->dest_node != NODE_WEIGHT_SCALE && pkt->dest_node != 0xFF) return;

    if (pkt->src_node == NODE_MAIN_CONTROLLER) {
        g_channel_ack_received = true;
    }

    switch (pkt->opcode) {
        case CMD_GET_WEIGHT:
            Serial.printf("[%s] >>> Kiosk Trigger: CMD_GET_WEIGHT received from Main Controller! <<<\n", TAG);
            g_orch_trigger = true;
            break;

        case CMD_PING:
            send_espnow_response(RESP_WEIGHT, 0.0f, 0);
            break;

        default:
            break;
    }
}

static void auto_channel_hunt() {
    Serial.printf("[%s] Starting Channel Hunt (Scanning 2.4 GHz channels 1-13)...\n", TAG);
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
                Serial.printf("[%s] >>> Locked to Main Controller on Channel %d! <<<\n", TAG, ch);
                return;
            }
            delay(5);
        }
    }
    Serial.printf("[%s] Notice: Defaulting to Channel %d.\n", TAG, g_active_channel);
    wifi_set_channel(g_active_channel);
    esp_now_del_peer(g_broadcast_mac);
    esp_now_add_peer(g_broadcast_mac, ESP_NOW_ROLE_COMBO, g_active_channel, NULL, 0);
}

/* =========================================================================
 * Precision Quiet Weight Sampling
 * ========================================================================= */
static float measure_quiet_stable_weight() {
    // 1. ENSURE RADIO IS 100% OFF FOR SENSING
    power_off_radio();

    if (!scale.wait_ready_timeout(1000)) {
        Serial.printf("[%s] Error: HX711 not ready (timeout after 1000ms)\n", TAG);
        return 0.0f;
    }

    Serial.printf("[%s] >>> QUIET SAMPLING STARTED: Platform stabilizing in radio silence... <<<\n", TAG);
    digitalWrite(PIN_STATUS_LED, LOW); // LED ON

    const int MAX_ATTEMPTS = 18;
    const int SAMPLES_PER_READ = 3;
    float prev_weight = 0.0f;
    float stable_weight = 0.0f;
    int stable_streak = 0;

    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        float current = scale.get_units(SAMPLES_PER_READ);
        if (current < 0.0f) current = 0.0f;

        Serial.printf("[%s] Sample %2d: %6.2f kg\n", TAG, attempt + 1, current);

        if (attempt > 0) {
            float delta = fabs(current - prev_weight);
            if (delta <= STABILITY_DELTA_KG && current >= 1.0f) {
                stable_streak++;
                if (stable_streak >= 2) {
                    stable_weight = (current + prev_weight) / 2.0f;
                    Serial.printf("[%s] ✅ Stable Weight Locked: %.2f kg (Streak=%d)\n", TAG, stable_weight, stable_streak);
                    break;
                }
            } else {
                stable_streak = 0;
            }
        }

        prev_weight = current;
        delay(100);
    }

    // Fallback if streak didn't lock: 5-sample average
    if (stable_weight == 0.0f && prev_weight >= 1.0f) {
        stable_weight = scale.get_units(5);
        if (stable_weight < 0.0f) stable_weight = 0.0f;
        Serial.printf("[%s] Fallback Average: %.2f kg\n", TAG, stable_weight);
    }

    digitalWrite(PIN_STATUS_LED, HIGH); // LED OFF
    return stable_weight;
}

/* =========================================================================
 * Arduino Setup & Loop
 * ========================================================================= */
void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println("\n========================================================");
    Serial.println("   MediBot Weight Scale Subsystem - Production Node");
    Serial.println("   ESP-12F Precision HX711 (Quiet Sampling Engine)");
    Serial.println("========================================================");

    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH);

    // 1. Initial Scale Hardware Check & Tare with Wi-Fi OFF
    power_off_radio();
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
        Serial.printf("[%s] Taring empty platform in radio silence...\n", TAG);
        scale.tare(10);
        Serial.printf("[%s] Tare complete. Baseline Offset: %ld\n", TAG, scale.get_offset());
    }

    // 2. Discover Main Controller Channel via short burst
    power_on_radio();
    auto_channel_hunt();

    Serial.println("[SETUP] Scale Ready! Listening for Kiosk command OR Step-on...");
    Serial.println("Commands: [m] Trigger measurement | [t] Tare | [r] Read raw ADC | [c <kg>] Calibrate | [+][-] Fine-tune factor");
}

void loop() {
    // -------------------------------------------------------------
    // Path 1: Kiosk Wireless Trigger (CMD_GET_WEIGHT)
    // -------------------------------------------------------------
    if (g_orch_trigger) {
        g_orch_trigger = false;

        // Perform measurement in 100% radio silence
        float weight = measure_quiet_stable_weight();

        // Wake up radio and transmit
        power_on_radio();
        uint8_t status = (weight > 0.0f) ? 0 : 1;
        send_espnow_response(RESP_WEIGHT, weight, status);

        // Keep radio on for 2 seconds to receive any follow-up ACKs
        delay(2000);
        // Then return to quiet monitoring
        power_off_radio();
    }

    // -------------------------------------------------------------
    // Path 2: Autonomous Step-On Detection (When Radio is Asleep)
    // -------------------------------------------------------------
    if (!g_radio_is_on && scale.wait_ready_timeout(100)) {
        float instant_weight = scale.get_units(1);
        if (instant_weight >= PATIENT_DETECT_MIN_KG) {
            Serial.printf("\n[%s] >>> Person detected on platform (%.1f kg)! Starting Quiet Measurement... <<<\n",
                          TAG, instant_weight);

            float final_weight = measure_quiet_stable_weight();

            if (final_weight >= PATIENT_DETECT_MIN_KG) {
                // Wake up radio and transmit
                power_on_radio();
                send_espnow_response(RESP_WEIGHT, final_weight, 0);

                Serial.printf("[%s] Waiting for patient to step off scale platform...\n", TAG);
                while (scale.get_units(1) > TARE_THRESHOLD_KG) {
                    delay(250);
                }
                Serial.printf("[%s] Patient stepped off. Returning to Quiet Sleep.\n", TAG);
                delay(500);
                power_off_radio();
            }
        }
    }

    // -------------------------------------------------------------
    // Path 3: Serial Monitor Diagnostics & Calibration
    // -------------------------------------------------------------
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        if (cmd == 'm') {
            Serial.println("\n--- MANUAL QUIET MEASUREMENT ---");
            float w = measure_quiet_stable_weight();
            power_on_radio();
            send_espnow_response(RESP_WEIGHT, w, (w > 0.0f) ? 0 : 1);
            power_off_radio();
        } else if (cmd == 't') {
            Serial.println("\n--- MANUAL TARE ---");
            power_off_radio();
            scale.tare(10);
            Serial.printf(">>> Platform Tared. New Offset: %ld\n\n", scale.get_offset());
        } else if (cmd == 'r') {
            Serial.printf(">>> Raw ADC: %ld | Units: %.2f kg | Factor: %.1f | Radio: %s\n",
                          scale.read(), scale.get_units(1), g_calibration_factor, g_radio_is_on ? "ON" : "OFF");
        } else if (cmd == 'c') {
            float known_kg = Serial.parseFloat();
            if (known_kg > 0.1f) {
                power_off_radio();
                long raw = scale.read_average(10);
                long offset = scale.get_offset();
                g_calibration_factor = (float)(raw - offset) / known_kg;
                scale.set_scale(g_calibration_factor);
                Serial.printf(">>> Calibrated with %.2f kg! Raw: %ld, Offset: %ld -> New Factor: %.2f\n\n",
                              known_kg, raw, offset, g_calibration_factor);
            } else {
                Serial.println("Usage: Send 'c <weight_in_kg>' (e.g. 'c 25.0' or 'c 10') with known weight on scale.");
            }
        } else if (cmd == '+') {
            g_calibration_factor += 50.0f;
            scale.set_scale(g_calibration_factor);
            Serial.printf(">>> Factor increased to: %.2f\n", g_calibration_factor);
        } else if (cmd == '-') {
            g_calibration_factor -= 50.0f;
            scale.set_scale(g_calibration_factor);
            Serial.printf(">>> Factor decreased to: %.2f\n", g_calibration_factor);
        }
    }

    delay(100);
}
