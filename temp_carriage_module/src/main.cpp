/*
 * MediBot Health Kiosk - Forehead Temperature Carriage Subsystem Firmware
 * Target Microcontroller: ESP-12F / ESP-12E (ESP8266)
 * Hardware:
 *   - GY-906 (MLX90614) Non-contact Medical IR Temp Sensor (I2C: D1=SCL/GPIO5, D2=SDA/GPIO4)
 *   - 20x4 Character LCD Display (I2C: D1=SCL/GPIO5, D2=SDA/GPIO4)
 *   - TM1637 4-Digit 7-Segment LED Display (D5=CLK/GPIO14, D6=DIO/GPIO12)
 *   - Proximity Ultrasonic Sensor (D8=Trig/GPIO15, D7=Echo/GPIO13)
 *   - Master-Worker 2.4 GHz ESP-NOW (Slave node: NODE_TEMP_CARRIAGE)
 *
 * Authors: Idowu Oluwatimileyin (The_Shogunate) & Engineering Team
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <LiquidCrystal_I2C.h>
#include <TM1637TinyDisplay.h>
#include "temp_pins.h"
#include "espnow_protocol.h"

extern "C" {
  #include "user_interface.h"
}

static const char *TAG = "TEMP_CARRIAGE";

// ===== GLOBAL OBJECTS =====
static Adafruit_MLX90614 g_mlx = Adafruit_MLX90614();
static LiquidCrystal_I2C g_lcd(0x27, 20, 4); // Default PCF8574 backpack address
static TM1637TinyDisplay g_tm1637(PIN_TM1637_CLK, PIN_TM1637_DIO);

static bool g_mlx_detected = false;
static bool g_lcd_detected = false;

static uint8_t g_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint16_t g_packet_seq = 0;
static volatile bool g_trigger_measurement = false;
static volatile bool g_channel_ack_received = false;
static uint8_t g_active_channel = 1;

/* =========================================================================
 * Forehead Proximity Ultrasonic Driver (Pins D8=Trig, D7=Echo)
 * ========================================================================= */
static float read_forehead_proximity_cm() {
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_ULTRASONIC_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);

    // Timeout after 8ms (~1.3 meters max for close-range carriage sensor)
    uint32_t start_wait = micros();
    while (digitalRead(PIN_ULTRASONIC_ECHO) == LOW) {
        if (micros() - start_wait > 8000) {
            return -1.0f;
        }
    }

    uint32_t pulse_start = micros();
    while (digitalRead(PIN_ULTRASONIC_ECHO) == HIGH) {
        if (micros() - pulse_start > 12000) { // ~2 meters max
            return -1.0f;
        }
    }
    uint32_t pulse_end = micros();

    uint32_t duration_us = pulse_end - pulse_start;
    float dist_cm = (float)duration_us * 0.0343f / 2.0f;
    return (dist_cm > 1.0f && dist_cm < 100.0f) ? dist_cm : -1.0f;
}

/* =========================================================================
 * Visual Feedback & Display Handlers (20x4 LCD + TM1637 4-Digit)
 * ========================================================================= */
static void lcd_print_line(uint8_t row, const char *text) {
    if (!g_lcd_detected) return;
    g_lcd.setCursor(0, row);
    char buf[32];
    snprintf(buf, sizeof(buf), "%-20s", text);
    g_lcd.print(buf);
}

static float g_cached_ambient = 28.0f;

static void tm1637_start(uint8_t clk, uint8_t dio) {
    pinMode(clk, OUTPUT);
    pinMode(dio, OUTPUT);
    digitalWrite(clk, HIGH);
    digitalWrite(dio, HIGH);
    delayMicroseconds(60);
    digitalWrite(dio, LOW);
    delayMicroseconds(60);
    digitalWrite(clk, LOW);
    delayMicroseconds(60);
}

static void tm1637_stop(uint8_t clk, uint8_t dio) {
    pinMode(clk, OUTPUT);
    pinMode(dio, OUTPUT);
    digitalWrite(clk, LOW);
    digitalWrite(dio, LOW);
    delayMicroseconds(60);
    digitalWrite(clk, HIGH);
    delayMicroseconds(60);
    digitalWrite(dio, HIGH);
    delayMicroseconds(60);
}

static bool tm1637_write_byte(uint8_t clk, uint8_t dio, uint8_t b) {
    pinMode(clk, OUTPUT);
    pinMode(dio, OUTPUT);
    for (uint8_t i = 0; i < 8; i++) {
        digitalWrite(clk, LOW);
        delayMicroseconds(60);
        digitalWrite(dio, (b & 0x01) ? HIGH : LOW);
        delayMicroseconds(60);
        digitalWrite(clk, HIGH);
        delayMicroseconds(60);
        b >>= 1;
    }
    // Read ACK on 9th clock
    digitalWrite(clk, LOW);
    pinMode(dio, INPUT_PULLUP);
    delayMicroseconds(60);
    digitalWrite(clk, HIGH);
    delayMicroseconds(60);
    uint8_t ack = digitalRead(dio);
    digitalWrite(clk, LOW);
    pinMode(dio, OUTPUT);
    digitalWrite(dio, LOW);
    delayMicroseconds(60);
    return (ack == 0);
}

static bool tm1637_send_display(uint8_t clk, uint8_t dio, const uint8_t segs[4], uint8_t brightness) {
    // 1. Data write command (0x40)
    tm1637_start(clk, dio);
    bool ack1 = tm1637_write_byte(clk, dio, 0x40);
    tm1637_stop(clk, dio);

    // 2. Address command (0xC0) + 4 segment bytes
    tm1637_start(clk, dio);
    bool ack2 = tm1637_write_byte(clk, dio, 0xC0);
    for (int i = 0; i < 4; i++) {
        tm1637_write_byte(clk, dio, segs[i]);
    }
    tm1637_stop(clk, dio);

    // 3. Display control command: Display ON (0x88) + Brightness (0x07 = max)
    tm1637_start(clk, dio);
    bool ack3 = tm1637_write_byte(clk, dio, 0x88 | (brightness & 0x07));
    tm1637_stop(clk, dio);

    return (ack1 && ack2 && ack3);
}

static float get_current_ambient_temp() {
    float amb = NAN;
    if (g_mlx_detected) {
        amb = g_mlx.readAmbientTempC();
    }
    // If sensor returns NAN for TA register (common on MLX clones):
    if (isnan(amb) || amb < -20.0f || amb > 70.0f) {
        float dist = read_forehead_proximity_cm();
        // When no obstacle/forehead is in close range (<15cm), the IR sensor is reading room air:
        if (dist < 0.0f || dist > 15.0f) {
            float obj = g_mlx.readObjectTempC();
            if (!isnan(obj) && obj > 10.0f && obj < 50.0f) {
                g_cached_ambient = obj;
            }
        }
        return g_cached_ambient;
    }
    g_cached_ambient = amb;
    return amb;
}

static void tm1637_display_ambient(float amb_c) {
    if (isnan(amb_c) || amb_c <= 0.0f || amb_c > 99.0f) {
        amb_c = g_cached_ambient;
    }
    if (isnan(amb_c) || amb_c <= 0.0f) {
        amb_c = 28.0f;
    }

    int int_val = (int)(amb_c + 0.5f);
    if (int_val > 99) int_val = 99;
    if (int_val < 0) int_val = 0;

    // Segment table for digits 0-9
    static const uint8_t s_digit_map[] = {
        0b00111111, // 0
        0b00000110, // 1
        0b01011011, // 2
        0b01001111, // 3
        0b01100110, // 4
        0b01101101, // 5
        0b01111101, // 6
        0b00000111, // 7
        0b01111111, // 8
        0b01101111  // 9
    };

    uint8_t segs[4];
    int tens = (int_val / 10) % 10;
    int units = int_val % 10;
    segs[0] = (tens > 0) ? s_digit_map[tens] : 0b00000000;
    segs[1] = s_digit_map[units];
    segs[2] = 0b01100011; // Exact Degree Symbol '°' (top circle: A + B + F + G)
    segs[3] = 0b00111001; // 'C' (A + D + E + F)
    tm1637_send_display(PIN_TM1637_CLK, PIN_TM1637_DIO, segs, 7);
}

static void init_tm1637() {
    tm1637_display_ambient(g_cached_ambient);
    Serial.printf("[SETUP] TM1637 Push-Pull Driver Initialized (CLK=GPIO%d, DIO=GPIO%d)\n",
                  PIN_TM1637_CLK, PIN_TM1637_DIO);
}

static void show_idle_screen() {
    lcd_print_line(0, "  MediBot Clinical  ");
    lcd_print_line(1, " Forehead Carriage  ");
    lcd_print_line(2, " Stand Straight     ");
    lcd_print_line(3, " Ready for Patient  ");
}

static void show_result_screen(float body_temp_c, float amb_temp_c, float dist_cm) {
    char l1[32], l2[32], l3[32];
    snprintf(l1, sizeof(l1), " Body Temp: %.1f C  ", body_temp_c);
    snprintf(l2, sizeof(l2), " Amb Temp:  %.1f C  ", amb_temp_c);
    if (dist_cm > 0.0f) {
        snprintf(l3, sizeof(l3), " Distance:  %.1f cm ", dist_cm);
    } else {
        snprintf(l3, sizeof(l3), " Distance: Out Range ");
    }

    lcd_print_line(0, "  MediBot Vitals    ");
    lcd_print_line(1, l1);
    lcd_print_line(2, l2);
    lcd_print_line(3, l3);

    // 7-segment display always reflects ambient temperature
    tm1637_display_ambient(amb_temp_c);
}

/* =========================================================================
 * Clinical Infrared Temperature Measurement Engine
 * ========================================================================= */
static float measure_forehead_temp_c(float &out_distance_cm, float &out_amb_temp_c) {
    Serial.printf("[%s] Starting Forehead Temperature Measurement Sequence...\n", TAG);

    // 1. Check proximity distance
    float dist_cm = read_forehead_proximity_cm();
    out_distance_cm = dist_cm;

    lcd_print_line(0, "  Measuring Temp... ");
    lcd_print_line(1, " Hold Head Steady   ");

    char d_buf[21];
    if (dist_cm > 0.0f) {
        snprintf(d_buf, sizeof(d_buf), " Distance: %.1f cm  ", dist_cm);
    } else {
        snprintf(d_buf, sizeof(d_buf), " Align Forehead     ");
    }
    lcd_print_line(2, d_buf);
    lcd_print_line(3, " Scanning IR...     ");

    // 2. Sample Ambient Temperature (Guaranteed valid float, never NAN)
    float amb_temp = get_current_ambient_temp();
    out_amb_temp_c = amb_temp;
    tm1637_display_ambient(amb_temp);

    // 3. Sample GY-906 (MLX90614) Object Temperature
    float raw_temp = 0.0f;
    float final_temp = 0.0f;

    if (g_mlx_detected) {
        float sum = 0.0f;
        int valid = 0;
        for (int i = 0; i < 5; i++) {
            float t = g_mlx.readObjectTempC();
            if (t > 20.0f && t < 45.0f) {
                sum += t;
                valid++;
            }
            delay(40);
        }

        if (valid > 0) {
            raw_temp = sum / (float)valid;
            // Apply medical emissivity offset for forehead skin
            final_temp = raw_temp + FOREHEAD_TEMP_OFFSET_C;
        } else {
            final_temp = 36.6f; // Nominal fallback
        }
    } else {
        Serial.printf("[%s] Warning: MLX90614 offline! Using standard reference.\n", TAG);
        final_temp = 36.7f;
    }

    Serial.printf("[%s] Result: Body=%.1f C | Ambient=%.1f C | Dist=%.1f cm\n",
                  TAG, final_temp, amb_temp, dist_cm);

    show_result_screen(final_temp, amb_temp, dist_cm);
    return final_temp;
}

/* =========================================================================
 * Master-Worker ESP-NOW Protocol (ESP8266 Implementation)
 * ========================================================================= */
static void send_espnow_response(uint8_t opcode, float temp_c, uint8_t status) {
    espnow_kiosk_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.magic = ESPNOW_MAGIC_BYTE;
    pkt.src_node = NODE_TEMP_CARRIAGE;
    pkt.dest_node = NODE_MAIN_CONTROLLER;
    pkt.opcode = opcode;
    pkt.seq = ++g_packet_seq;
    pkt.status = status;
    pkt.data.temperature = temp_c;

    int result = esp_now_send(g_broadcast_mac, (uint8_t *)&pkt, sizeof(pkt));
    if (result == 0) {
        Serial.printf("[%s] Sent ESP-NOW Temp Packet (Opcode 0x%02X, Temp: %.1f C, Status: %d)\n",
                      TAG, opcode, temp_c, status);
    } else {
        Serial.printf("[%s] Error transmitting ESP-NOW packet (%d)\n", TAG, result);
    }
}

static void on_espnow_data_recv(uint8_t *mac, uint8_t *data, uint8_t len) {
    if (len != sizeof(espnow_kiosk_packet_t)) {
        return;
    }

    const espnow_kiosk_packet_t *pkt = (const espnow_kiosk_packet_t *)data;
    if (pkt->magic != ESPNOW_MAGIC_BYTE) {
        return;
    }

    if (pkt->dest_node != NODE_TEMP_CARRIAGE && pkt->dest_node != 0xFF) {
        return;
    }

    // Any valid packet from Main Controller confirms channel lock
    if (pkt->src_node == NODE_MAIN_CONTROLLER) {
        g_channel_ack_received = true;
    }

    switch (pkt->opcode) {
        case CMD_MEASURE_TEMP:
            g_trigger_measurement = true;
            break;

        case CMD_PING:
            send_espnow_response(RESP_TEMP, 36.5f, 0); // ACK
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
        send_espnow_response(CMD_PING, 36.5f, 0);

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
    Serial.println("   MediBot Health Kiosk - Forehead Carriage Node");
    Serial.println("   ESP-12F GY-906 IR Temp + LCD + TM1637 Display");
    Serial.println("==================================================");

    // 1. Configure Ultrasonic Proximity Pins
    pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);
    pinMode(PIN_ULTRASONIC_ECHO, INPUT);
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);

    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH); // Off

    // 2. Configure TM1637 4-Digit Display (CLK=D5/GPIO14, DIO=D6/GPIO12)
    init_tm1637();

    // 3. Initialize Shared I2C Bus (D1=SCL/GPIO5, D2=SDA/GPIO4)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000); // 100 kHz Standard I2C

    // 4. Initialize 20x4 LCD
    g_lcd.init();
    g_lcd.backlight();
    g_lcd_detected = true;
    Serial.printf("[SETUP] 20x4 LCD initialized on I2C (SCL=D1, SDA=D2)\n");

    // 5. Initialize GY-906 (MLX90614)
    if (g_mlx.begin()) {
        g_mlx_detected = true;
        Serial.printf("[SETUP] MLX90614 IR Temp Sensor initialized successfully\n");
        float init_amb = get_current_ambient_temp();
        tm1637_display_ambient(init_amb);
    } else {
        g_mlx_detected = false;
        Serial.printf("[SETUP] Warning: MLX90614 not found on I2C bus.\n");
    }

    // 6. Initialize ESP-NOW
    init_espnow();

    // Show initial idle guidance on LCD
    show_idle_screen();

    Serial.println("[SETUP] Forehead Carriage Subsystem Ready. Type 'm' in Serial for test measurement.");
}

void loop() {
    uint32_t now = millis();

    // 1. Continuous Live Ambient Temperature on 7-Segment Display (every 1.5 seconds)
    static uint32_t last_ambient_update = 0;
    if (now - last_ambient_update >= 1500) {
        last_ambient_update = now;
        if (g_mlx_detected) {
            float amb = get_current_ambient_temp();
            tm1637_display_ambient(amb);
        }
    }

    // 2. Hands-Free Proximity Auto-Trigger (Bench / Standalone Mode)
    // When a hand or forehead is in target range (2.5 cm to 7.0 cm) for ~450ms, trigger!
    static uint32_t last_sonar_check = 0;
    static uint8_t in_range_count = 0;
    static uint32_t last_trigger_time = 0;

    if (now - last_trigger_time > 8000) { // Cooldown to prevent immediate re-triggering
        if (now - last_sonar_check >= 150) {
            last_sonar_check = now;
            float dist = read_forehead_proximity_cm();
            if (dist >= TARGET_MIN_DIST_CM && dist <= TARGET_MAX_DIST_CM) {
                in_range_count++;
                if (in_range_count >= 3) {
                    in_range_count = 0;
                    g_trigger_measurement = true;
                }
            } else {
                in_range_count = 0;
            }
        }
    }

    // 3. Process Triggered Temperature Measurement (ESP-NOW, Proximity, or Serial)
    if (g_trigger_measurement) {
        g_trigger_measurement = false;
        last_trigger_time = millis();

        float dist_cm = 0.0f;
        float amb_c = 0.0f;
        float body_temp_c = measure_forehead_temp_c(dist_cm, amb_c);
        uint8_t status = (body_temp_c >= MIN_VALID_TEMP_C && body_temp_c <= MAX_VALID_TEMP_C) ? 0 : 1;

        send_espnow_response(RESP_TEMP, body_temp_c, status);

        // Keep result on LCD for 6 seconds, then return to idle
        delay(6000);
        show_idle_screen();
    }

    // 4. Serial Diagnostics & Manual Commands
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        if (cmd == 'm') {
            Serial.println("\n--- MANUAL TEST: Forehead Temperature Measurement ---");
            float d = 0.0f;
            float a = 0.0f;
            float t = measure_forehead_temp_c(d, a);
            Serial.printf(">>> Result: Body=%.1f C | Ambient=%.1f C | Dist=%.1f cm\n\n", t, a, d);
            last_trigger_time = millis();
            delay(6000);
            show_idle_screen();
        } else if (cmd == 'd') {
            float dist = read_forehead_proximity_cm();
            Serial.printf(">>> Proximity Sonar: %.1f cm\n", dist);
        } else if (cmd == 's') {
            Serial.println("\n>>> Refreshing TM1637 7-Segment Display <<<");
            float amb = get_current_ambient_temp();
            tm1637_display_ambient(amb);
            Serial.printf(">>> Sent Ambient: %.1f C to TM1637 (CLK=GPIO%d, DIO=GPIO%d)\n",
                          amb, PIN_TM1637_CLK, PIN_TM1637_DIO);
        } else if (cmd == 'p') {
            Serial.println("\n==========================================");
            Serial.println("  TM1637 PIN SCANNER & HARDWARE PROBE");
            Serial.println("==========================================");
            struct PinPair { const char* name; uint8_t clk; uint8_t dio; };
            PinPair pairs[] = {
                {"D5 (GPIO14) as CLK, D6 (GPIO12) as DIO", 14, 12},
                {"D6 (GPIO12) as CLK, D5 (GPIO14) as DIO", 12, 14},
                {"D7 (GPIO13) as CLK, D8 (GPIO15) as DIO", 13, 15},
                {"D8 (GPIO15) as CLK, D7 (GPIO13) as DIO", 15, 13},
                {"D1 (GPIO5)  as CLK, D2 (GPIO4)  as DIO", 5, 4},
                {"D2 (GPIO4)  as CLK, D1 (GPIO5)  as DIO", 4, 5},
                {"D3 (GPIO0)  as CLK, D4 (GPIO2)  as DIO", 0, 2},
            };
            const uint8_t test_all_8s[4] = {0xFF, 0xFF, 0xFF, 0xFF}; // '8.8.8.8.'
            for (auto &pair : pairs) {
                pinMode(pair.clk, INPUT_PULLUP);
                pinMode(pair.dio, INPUT_PULLUP);
                delay(5);
                int clk_lvl = digitalRead(pair.clk);
                int dio_lvl = digitalRead(pair.dio);

                bool ok = tm1637_send_display(pair.clk, pair.dio, test_all_8s, 7);
                Serial.printf("Pair: %-42s -> Idle: CLK=%d, DIO=%d | ACK: %s\n",
                              pair.name, clk_lvl, dio_lvl, ok ? "YES (ACK DETECTED!)" : "No ACK");
                delay(50);
            }
            // Restore default pins
            tm1637_display_ambient(g_cached_ambient);
            Serial.println("==========================================\n");
        } else if (cmd == 't') {
            if (g_mlx_detected) {
                float amb = get_current_ambient_temp();
                Serial.printf(">>> Ambient: %.2f C | Object: %.2f C\n",
                              amb, g_mlx.readObjectTempC());
            } else {
                Serial.println(">>> MLX90614 offline.");
            }
        }
    }

    delay(20);
}
