/*
 * MediBot Health Kiosk - Weight Scale Subsystem Firmware
 * Target Microcontroller: ESP-12F / ESP-12E (ESP8266)
 * Hardware:
 *   - 4x Strain Gauge Load Cells in Wheatstone Bridge Configuration
 *   - HX711 24-bit ADC Amplifier (DOUT=GPIO4/D2, SCK=GPIO5/D1)
 *   - Master-Worker 2.4 GHz ESP-NOW (Slave node: NODE_WEIGHT_SCALE)
 *
 * Sequential Orchestration & Quiet Human Sensing Engine:
 *   1. Standby: Node listens in low-power RX for Main Controller's CMD_GET_WEIGHT.
 *   2. Command Received: Wi-Fi radio & ESP-NOW are COMPLETELY SHUT DOWN (Zero RF, pure DC ~15mA).
 *   3. Human Verification: Actively waits for human weight (>= 15kg) to be on the platform.
 *      Never takes or transmits empty platform readings (0 kg).
 *   4. Quiet Stabilization: Samples load in complete radio silence until weight locks within ±0.25kg.
 *   5. Wake & Transmit: Wakes up radio, broadcasts RESP_WEIGHT to Main Controller, returns to RX standby.
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

// Hardware Objects & State
static HX711 scale;
static float g_calibration_factor = DEFAULT_CALIBRATION_FACTOR;
static uint8_t g_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint16_t g_packet_seq = 0;
static uint8_t g_active_channel = 1;

static volatile bool g_orch_trigger = false;
static volatile bool g_send_immediate_ack = false;
static volatile bool g_send_ping_ack = false;
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
    delay(25);

    WiFi.mode(WIFI_STA);
    wifi_set_channel(g_active_channel);

    if (esp_now_init() == 0) {
        esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
        esp_now_register_recv_cb(on_espnow_data_recv);
        esp_now_add_peer(g_broadcast_mac, ESP_NOW_ROLE_COMBO, g_active_channel, NULL, 0);
        g_radio_is_on = true;
        Serial.printf("[%s] [POWER] Wi-Fi Active on Channel %d (Listening for Kiosk)\n", TAG, g_active_channel);
    } else {
        Serial.printf("[%s] [POWER] Error re-initializing ESP-NOW\n", TAG);
    }
}

static void power_off_radio() {
    if (!g_radio_is_on) return;

    Serial.printf("[%s] [POWER] Shutting DOWN Wi-Fi radio & ESP-NOW...\n", TAG);
    esp_now_deinit();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    delay(150); // Settling delay: caps discharge RF ripple, power rail is pure flat DC
    g_radio_is_on = false;
    Serial.printf("[%s] [POWER] ✅ Radio 100%% OFF. Zero RF emissions, pure DC active.\n", TAG);
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

    // For Immediate ACK or Ping, send a single packet instantly without delay
    if (status == STATUS_BUSY_MEASURING || opcode == CMD_PING) {
        int res = esp_now_send(g_broadcast_mac, (uint8_t *)&pkt, sizeof(pkt));
        Serial.printf("[%s] Sent ESP-NOW ACK (Opcode 0x%02X, Status: %d, Res: %d)\n",
                      TAG, opcode, status, res);
        return;
    }

    // Send 3 burst transmissions for final data to guarantee delivery
    for (int i = 0; i < 3; i++) {
        int res = esp_now_send(g_broadcast_mac, (uint8_t *)&pkt, sizeof(pkt));
        if (res == 0) {
            Serial.printf("[%s] Sent ESP-NOW Weight Packet #%d (Weight: %.2f kg, Status: %d)\n",
                          TAG, i + 1, value, status);
        }
        delay(15);
    }
}

// ISR Callback: Must NEVER call delay() or heavy operations in sys context
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
            // Set flags for loop() to send Immediate ACK and begin measurement in user context
            g_send_immediate_ack = true;
            g_orch_trigger = true;
            break;

        case CMD_ACK:
            break;

        case CMD_PING:
            g_send_ping_ack = true;
            break;

        default:
            break;
    }
}

static void auto_channel_hunt() {
    Serial.printf("[%s] Starting Channel Hunt (Scanning 2.4 GHz channels)...\n", TAG);

    // Prioritized channel order: Primary non-overlapping channels (1, 6, 11) first
    const uint8_t scan_order[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
    const size_t num_channels = sizeof(scan_order) / sizeof(scan_order[0]);

    for (size_t i = 0; i < num_channels; i++) {
        uint8_t ch = scan_order[i];
        wifi_set_channel(ch);
        esp_now_del_peer(g_broadcast_mac);
        esp_now_add_peer(g_broadcast_mac, ESP_NOW_ROLE_COMBO, ch, NULL, 0);

        g_channel_ack_received = false;
        send_espnow_response(CMD_PING, 0.0f, 0);
        delay(15);
        send_espnow_response(CMD_PING, 0.0f, 0);

        uint32_t start_ms = millis();
        while (millis() - start_ms < 120) {
            if (g_channel_ack_received) {
                g_active_channel = ch;
                Serial.printf("[%s] >>> Locked to Main Controller on Channel %d! <<<\n", TAG, ch);
                return;
            }
            delay(5);
        }
    }
    Serial.printf("[%s] Notice: Defaulting to Channel 1.\n", TAG);
    g_active_channel = 1;
    wifi_set_channel(1);
    esp_now_del_peer(g_broadcast_mac);
    esp_now_add_peer(g_broadcast_mac, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
}

/* =========================================================================
 * Precision Quiet Human Weight Measurement
 * 
 * Rules:
 *  1. Wi-Fi must be 100% OFF before sampling.
 *  2. Must wait until human load (>= min_threshold_kg) is actually detected.
 *     Never locks or sends empty platform / zero readings.
 *  3. Takes samples in complete radio silence until reading stabilizes.
 * ========================================================================= */
static float measure_human_quiet_weight(float min_threshold_kg, uint32_t timeout_ms) {
    // 1. Ensure radio is 100% OFF before touching the ADC
    power_off_radio();

    if (!scale.wait_ready_timeout(1000)) {
        Serial.printf("[%s] Error: HX711 not ready\n", TAG);
        return 0.0f;
    }

    Serial.printf("[%s] >>> WAITING FOR PATIENT: Step onto platform (Min: %.1f kg)... <<<\n",
                  TAG, min_threshold_kg);

    // 2. Wait actively for human weight to appear on the platform
    uint32_t wait_start = millis();
    bool human_present = false;

    while (millis() - wait_start < timeout_ms) {
        if (scale.wait_ready_timeout(120)) {
            float instant = scale.get_units(1);
            if (instant >= min_threshold_kg) {
                human_present = true;
                Serial.printf("[%s] >>> Human detected on platform (%.1f kg)! Stabilizing... <<<\n",
                              TAG, instant);
                break;
            }
        }
        delay(80);
    }

    if (!human_present) {
        Serial.printf("[%s] Timeout: No patient detected on scale within %u ms.\n", TAG, timeout_ms);
        return 0.0f;
    }

    // 3. Human is confirmed on platform -> Perform precision stabilization in radio silence
    digitalWrite(PIN_STATUS_LED, LOW); // LED ON
    Serial.printf("[%s] >>> QUIET SAMPLING: Locking stable weight in radio silence... <<<\n", TAG);

    const int MAX_ATTEMPTS = 16;
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
            if (delta <= STABILITY_DELTA_KG && current >= min_threshold_kg) {
                stable_streak++;
                if (stable_streak >= 2) {
                    stable_weight = (current + prev_weight) / 2.0f;
                    Serial.printf("[%s] ✅ Stable Human Weight Locked: %.2f kg (Streak=%d)\n",
                                  TAG, stable_weight, stable_streak);
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
    if (stable_weight == 0.0f && prev_weight >= min_threshold_kg) {
        stable_weight = scale.get_units(5);
        if (stable_weight < 0.0f) stable_weight = 0.0f;
        Serial.printf("[%s] Fallback Average: %.2f kg\n", TAG, stable_weight);
    }

    digitalWrite(PIN_STATUS_LED, HIGH); // LED OFF
    return stable_weight;
}

/* =========================================================================
 * Arduino Setup & Main Loop
 * ========================================================================= */
void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println("\n========================================================");
    Serial.println("   MediBot Weight Scale Subsystem - Production Node");
    Serial.println("   ESP-12F Precision HX711 (Orchestrated Quiet Sampling)");
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

    // 3. Standby: Radio stays in RX listening for Kiosk 'Take Measurement' command
    Serial.println("[SETUP] Scale Ready! Listening in RX for Kiosk 'Take Measurement' command...");
    Serial.println("Commands: [m] Trigger measurement | [t] Tare | [r] Read raw ADC | [c <kg>] Calibrate | [+][-] Fine-tune factor");
}

void loop() {
    // -------------------------------------------------------------
    // Path 0: Immediate Handshake Responses (Executed safely in user task context)
    // -------------------------------------------------------------
    if (g_send_immediate_ack) {
        g_send_immediate_ack = false;
        Serial.printf("[%s] >>> Kiosk Trigger: CMD_GET_WEIGHT received! Sending Immediate ACK... <<<\n", TAG);
        send_espnow_response(RESP_WEIGHT, 0.0f, STATUS_BUSY_MEASURING);
    }

    if (g_send_ping_ack) {
        g_send_ping_ack = false;
        send_espnow_response(RESP_WEIGHT, 0.0f, 0);
    }

    // -------------------------------------------------------------
    // Path 1: Kiosk Orchestrated Trigger (CMD_GET_WEIGHT)
    // -------------------------------------------------------------
    if (g_orch_trigger) {
        g_orch_trigger = false;

        // 1. Shut off radio completely & wait for human (>= 15kg) to be on scale
        float final_weight = measure_human_quiet_weight(PATIENT_DETECT_MIN_KG, 25000);

        // 2. Turn radio ON only after measurement is ready to send
        power_on_radio();

        uint8_t status = (final_weight >= PATIENT_DETECT_MIN_KG) ? STATUS_OK : STATUS_ERROR;
        send_espnow_response(RESP_WEIGHT, final_weight, status);

        if (status == STATUS_OK) {
            Serial.printf("[%s] ✅ Vitals Step 1 Complete! Weight %.2f kg sent to Main Controller.\n",
                          TAG, final_weight);
        } else {
            Serial.printf("[%s] ⚠️ Notice: Measurement timeout/no patient. Sent status=%d.\n",
                          TAG, status);
        }
    }

    // -------------------------------------------------------------
    // Path 2: Serial Monitor Diagnostics & Interactive Calibration
    // -------------------------------------------------------------
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        if (cmd == 'm') {
            Serial.println("\n--- MANUAL QUIET TEST MEASUREMENT ---");
            // For manual bench testing: accepts test weights >= 0.5 kg (e.g. 1kg weight)
            float w = measure_human_quiet_weight(0.5f, 8000);
            power_on_radio();
            send_espnow_response(RESP_WEIGHT, w, (w > 0.0f) ? 0 : 1);
        } else if (cmd == 't') {
            Serial.println("\n--- MANUAL TARE ---");
            power_off_radio();
            scale.tare(10);
            Serial.printf(">>> Platform Tared. New Offset: %ld\n\n", scale.get_offset());
            power_on_radio();
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
                power_on_radio();
            } else {
                Serial.println("Usage: Send 'c <weight_in_kg>' (e.g. 'c 25.0' or 'c 1.0') with known weight on scale.");
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

    // -------------------------------------------------------------
    // Path 3: Periodic Channel Verification & Self-Healing Sync (Every 15s)
    // -------------------------------------------------------------
    static uint32_t last_channel_check = 0;
    if (millis() - last_channel_check >= 15000) {
        last_channel_check = millis();
        if (g_radio_is_on && !g_orch_trigger) {
            if (!g_channel_ack_received) {
                Serial.printf("[%s] [CHANNEL SYNC] Main Controller not heard. Hunting active channel...\n", TAG);
                auto_channel_hunt();
            } else {
                g_channel_ack_received = false;
                send_espnow_response(CMD_PING, 0.0f, 0);
            }
        }
    }

    delay(20);
}
