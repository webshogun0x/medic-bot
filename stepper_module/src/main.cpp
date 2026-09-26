/*
 * MediBot Health Kiosk - Vertical Stepper Gantry Subsystem Firmware
 * Target Microcontroller: ESP-WROOM-32
 * Hardware:
 *   - TB6600 Stepper Driver (PUL=GPIO18, DIR=GPIO19, ENA=GPIO21)
 *   - Hardware Limit Switches:
 *       • Bottom Home Switch: GPIO 4 (Active LOW)
 *       • Top Safety Switch:  GPIO 5 (Active LOW)
 *   - Master-Worker 2.4 GHz ESP-NOW (Slave node: NODE_STEPPER_GANTRY)
 *   - Third-Party Engine: AccelStepper (smooth acceleration & deceleration)
 *
 * Authors: Idowu Oluwatimileyin (The_Shogunate) & Engineering Team
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <AccelStepper.h>
#include "stepper_pins.h"
#include "espnow_protocol.h"

static const char *TAG = "STEPPER_NODE";

// ===== GLOBAL OBJECTS & STATE =====
static AccelStepper g_stepper(AccelStepper::DRIVER, PIN_STEPPER_PUL, PIN_STEPPER_DIR);
static uint8_t g_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint16_t g_packet_seq = 0;
static volatile bool g_channel_ack_received = false;
static uint8_t g_active_channel = 1;

static bool g_is_homed = false;
static float g_current_height_cm = HOME_BASELINE_HEIGHT_CM;

// Pending command flags
static volatile bool g_trigger_move = false;
static volatile bool g_trigger_home = false;
static volatile bool g_emergency_halt = false;
static volatile float g_target_height_req = HOME_BASELINE_HEIGHT_CM;

/* =========================================================================
 * Hardware Driver & Motor Power Control
 * ========================================================================= */
static void set_motor_power(bool enable) {
    // TB6600 ENA pin: LOW = Energized / Holding Torque, HIGH = De-energized / Freewheel
    digitalWrite(PIN_STEPPER_ENA, enable ? LOW : HIGH);
    digitalWrite(PIN_STATUS_LED, enable ? HIGH : LOW);
}

/* =========================================================================
 * Homing Sequence (Bottom Home Limit Switch on GPIO 4)
 * ========================================================================= */
static bool home_gantry_to_bottom() {
    Serial.printf("[%s] Starting Homing Sequence to Bottom Limit Switch (GPIO %d)...\n", TAG, PIN_LIMIT_HOME_BOTTOM);
    set_motor_power(true);

    g_stepper.setMaxSpeed(HOMING_SPEED_STEPS_SEC);
    g_stepper.setAcceleration(ACCELERATION_STEPS_SEC2);

    // If already pressing switch, back off slightly first
    if (digitalRead(PIN_LIMIT_HOME_BOTTOM) == LOW) {
        Serial.printf("[%s] Switch already depressed. Backing off...\n", TAG);
        g_stepper.move(1000); // Step upwards
        while (g_stepper.distanceToGo() != 0) {
            g_stepper.run();
            delayMicroseconds(100);
        }
    }

    // Drive downwards towards bottom switch
    long max_homing_steps = (long)(MAX_TRAVEL_CM * STEPS_PER_CM * 1.5f);
    g_stepper.move(-max_homing_steps);

    uint32_t start_time = millis();
    bool switch_hit = false;

    while (g_stepper.distanceToGo() != 0) {
        // Active LOW switch check
        if (digitalRead(PIN_LIMIT_HOME_BOTTOM) == LOW) {
            switch_hit = true;
            break;
        }

        if (g_emergency_halt || (millis() - start_time > 30000)) {
            Serial.printf("[%s] Homing aborted or timed out!\n", TAG);
            break;
        }

        g_stepper.run();
        yield();
    }

    g_stepper.stop();
    g_stepper.setCurrentPosition(0);

    if (switch_hit) {
        // Back off 3 mm so switch contacts are not strained
        g_stepper.move(600);
        while (g_stepper.distanceToGo() != 0) {
            g_stepper.run();
            yield();
        }
        g_stepper.setCurrentPosition(0);
        g_is_homed = true;
        g_current_height_cm = HOME_BASELINE_HEIGHT_CM;
        Serial.printf("[%s] Homing SUCCESSFUL! Gantry baseline set to %.1f cm\n", TAG, g_current_height_cm);
        return true;
    }

    Serial.printf("[%s] Error: Bottom limit switch was not detected during homing.\n", TAG);
    return false;
}

/* =========================================================================
 * Vertical Motion Engine with Top Safety Switch Protection (GPIO 5)
 * ========================================================================= */
static bool move_gantry_to_height(float target_height_cm) {
    if (!g_is_homed) {
        Serial.printf("[%s] System not homed! Homing first before motion...\n", TAG);
        if (!home_gantry_to_bottom()) {
            return false;
        }
    }

    // Clamp within physical structural boundaries
    if (target_height_cm < HOME_BASELINE_HEIGHT_CM) {
        target_height_cm = HOME_BASELINE_HEIGHT_CM;
    }
    float max_allowed = HOME_BASELINE_HEIGHT_CM + MAX_TRAVEL_CM;
    if (target_height_cm > max_allowed) {
        target_height_cm = max_allowed;
    }

    float delta_cm = target_height_cm - HOME_BASELINE_HEIGHT_CM;
    long target_step_pos = (long)(delta_cm * STEPS_PER_CM);

    Serial.printf("[%s] Moving gantry: Current=%.1f cm -> Target=%.1f cm (Target Steps: %ld)\n",
                  TAG, g_current_height_cm, target_height_cm, target_step_pos);

    set_motor_power(true);
    g_stepper.setMaxSpeed(MAX_SPEED_STEPS_SEC);
    g_stepper.setAcceleration(ACCELERATION_STEPS_SEC2);
    g_stepper.moveTo(target_step_pos);

    bool safety_abort = false;

    while (g_stepper.distanceToGo() != 0) {
        // 1. Check Top Safety Limit Switch (GPIO 5)
        if (digitalRead(PIN_LIMIT_SAFETY_TOP) == LOW) {
            Serial.printf("[%s] ⚠️ CRITICAL: Top safety limit switch triggered! Halting motion.\n", TAG);
            safety_abort = true;
            break;
        }

        // 2. Check Emergency Halt Signal
        if (g_emergency_halt) {
            Serial.printf("[%s] 🛑 Emergency Halt Received! Motor stopped.\n", TAG);
            safety_abort = true;
            break;
        }

        g_stepper.run();
        yield();
    }

    g_stepper.stop();

    if (safety_abort) {
        set_motor_power(false);
        return false;
    }

    g_current_height_cm = target_height_cm;
    Serial.printf("[%s] Motion COMPLETE. Forehead carriage positioned at %.1f cm\n", TAG, g_current_height_cm);
    return true;
}

/* =========================================================================
 * Master-Worker ESP-NOW Communication Protocol
 * ========================================================================= */
static void send_espnow_response(uint8_t opcode, float current_height, uint8_t status) {
    espnow_kiosk_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.magic = ESPNOW_MAGIC_BYTE;
    pkt.src_node = NODE_STEPPER_GANTRY;
    pkt.dest_node = NODE_MAIN_CONTROLLER;
    pkt.opcode = opcode;
    pkt.seq = ++g_packet_seq;
    pkt.status = status;
    pkt.data.height = current_height;
    pkt.data.gantry_pos.current_position_cm = current_height;
    pkt.data.gantry_pos.aligned = (status == 0 ? 1 : 0);

    esp_err_t res = esp_now_send(g_broadcast_mac, (uint8_t *)&pkt, sizeof(pkt));
    if (res == ESP_OK) {
        Serial.printf("[%s] Sent ESP-NOW ACK (Opcode 0x%02X, Pos: %.1f cm, Status: %d)\n",
                      TAG, opcode, current_height, status);
    } else {
        Serial.printf("[%s] Error transmitting ESP-NOW packet (%d)\n", TAG, res);
    }
}

static void on_espnow_data_recv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len != sizeof(espnow_kiosk_packet_t)) {
        return;
    }

    const espnow_kiosk_packet_t *pkt = (const espnow_kiosk_packet_t *)data;
    if (pkt->magic != ESPNOW_MAGIC_BYTE) {
        return;
    }

    if (pkt->dest_node != NODE_STEPPER_GANTRY && pkt->dest_node != 0xFF) {
        return;
    }

    // Any valid packet from Main Controller confirms channel lock
    if (pkt->src_node == NODE_MAIN_CONTROLLER) {
        g_channel_ack_received = true;
    }

    switch (pkt->opcode) {
        case CMD_MOVE_CARRIAGE: {
            // Target height can be in data.height or data.stepper_cmd.target_height_cm
            float target = pkt->data.height;
            if (target <= 0.0f) target = pkt->data.stepper_cmd.target_height_cm;
            g_target_height_req = target;
            g_trigger_move = true;
            g_emergency_halt = false;
            break;
        }

        case CMD_RETURN_HOME:
            g_trigger_home = true;
            g_emergency_halt = false;
            break;

        case CMD_EMERGENCY_HALT:
            g_emergency_halt = true;
            set_motor_power(false);
            g_stepper.stop();
            break;

        case CMD_PING:
            send_espnow_response(RESP_STEPPER_ACK, g_current_height_cm, 0);
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
        send_espnow_response(CMD_PING, g_current_height_cm, 0);

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

    esp_now_peer_info_t peer_info;
    memset(&peer_info, 0, sizeof(peer_info));
    memcpy(peer_info.peer_addr, g_broadcast_mac, 6);
    peer_info.channel = 0;
    peer_info.encrypt = false;

    if (esp_now_add_peer(&peer_info) != ESP_OK) {
        Serial.printf("[%s] Error adding broadcast peer\n", TAG);
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
    Serial.println("   MediBot Health Kiosk - Vertical Stepper Gantry");
    Serial.println("   ESP-WROOM-32 TB6600 Driver with AccelStepper");
    Serial.println("==================================================");

    // 1. Configure Stepper Driver Outputs
    pinMode(PIN_STEPPER_PUL, OUTPUT);
    pinMode(PIN_STEPPER_DIR, OUTPUT);
    pinMode(PIN_STEPPER_ENA, OUTPUT);
    pinMode(PIN_STATUS_LED, OUTPUT);
    set_motor_power(false);

    // 2. Configure Hardware Limit Switches with Internal Pull-Ups
    pinMode(PIN_LIMIT_HOME_BOTTOM, INPUT_PULLUP);
    pinMode(PIN_LIMIT_SAFETY_TOP, INPUT_PULLUP);

    Serial.printf("[SETUP] Stepper Pins: PUL=%d, DIR=%d, ENA=%d\n",
                  PIN_STEPPER_PUL, PIN_STEPPER_DIR, PIN_STEPPER_ENA);
    Serial.printf("[SETUP] Limit Switches: Bottom Home=GPIO %d (Pin 4), Top Safety=GPIO %d (Pin 5)\n",
                  PIN_LIMIT_HOME_BOTTOM, PIN_LIMIT_SAFETY_TOP);

    // 3. Initialize AccelStepper parameters
    g_stepper.setMaxSpeed(MAX_SPEED_STEPS_SEC);
    g_stepper.setAcceleration(ACCELERATION_STEPS_SEC2);

    // 4. Initialize ESP-NOW
    init_espnow();

    // 5. Initial homing at boot
    Serial.println("[SETUP] Performing boot homing sequence...");
    home_gantry_to_bottom();

    Serial.println("[SETUP] Stepper Subsystem Ready. Type 'h' to home, 'm 175' to move to 175cm, 's' for switch status.");
}

void loop() {
    // 1. Process ESP-NOW Move Command
    if (g_trigger_move) {
        g_trigger_move = false;
        bool ok = move_gantry_to_height(g_target_height_req);
        send_espnow_response(RESP_STEPPER_ACK, g_current_height_cm, ok ? 0 : 1);
    }

    // 2. Process ESP-NOW Home Command
    if (g_trigger_home) {
        g_trigger_home = false;
        bool ok = home_gantry_to_bottom();
        send_espnow_response(RESP_STEPPER_ACK, g_current_height_cm, ok ? 0 : 1);
    }

    // 3. Serial Diagnostics & Manual Control
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        if (cmd == 'h') {
            Serial.println("\n--- MANUAL HOMING ---");
            home_gantry_to_bottom();
        } else if (cmd == 's') {
            int btm = digitalRead(PIN_LIMIT_HOME_BOTTOM);
            int top = digitalRead(PIN_LIMIT_SAFETY_TOP);
            Serial.printf(">>> Switches: Bottom Home (GPIO 4)=%s | Top Safety (GPIO 5)=%s\n",
                          btm == LOW ? "PRESSED (ACTIVE)" : "OPEN",
                          top == LOW ? "PRESSED (ACTIVE)" : "OPEN");
        } else if (cmd == 'm') {
            float target = Serial.parseFloat();
            if (target > 0.0f) {
                Serial.printf("\n--- MANUAL MOVE TO %.1f CM ---\n", target);
                move_gantry_to_height(target);
            } else {
                Serial.println("Usage: m 175 (to move to 175 cm)");
            }
        } else if (cmd == 'e') {
            Serial.println("\n--- EMERGENCY HALT TRIGGERED ---");
            g_emergency_halt = true;
            set_motor_power(false);
            g_stepper.stop();
        }
    }

    delay(10);
}
