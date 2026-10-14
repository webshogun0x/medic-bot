/*
 * =========================================================================
 * MediBot Weight Scale Subsystem - Pure Hardware & Power Diagnostic Test
 * Target MCU: ESP-12F / ESP-12E (ESP8266)
 * 
 * Objectives:
 *  1. Completely SHUT DOWN Wi-Fi and ESP-NOW radio to prevent current surges,
 *     brownouts, and RF coupling into the load cell bridge.
 *  2. Continuous, unscaled 24-bit Raw ADC streaming from HX711 (GPIO 4 DOUT, GPIO 5 SCK).
 *  3. Real-time deflection tracking (Baseline Delta, Min/Max Peak Hold).
 *  4. Hardware DOUT state diagnostics.
 * =========================================================================
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "HX711.h"

// Hardware Pin Configuration
// On ESP-12F:
// GPIO 4 = D2 (DOUT)
// GPIO 5 = D1 (SCK)
// GPIO 2 = On-board Blue LED (Active LOW)
#define PIN_HX711_DOUT 4
#define PIN_HX711_SCK  5
#define PIN_STATUS_LED 2

static HX711 scale;
static long s_baseline_raw = 0;
static long s_min_raw = 2147483647L;
static long s_max_raw = -2147483648L;
static uint32_t s_sample_count = 0;
static bool s_fast_mode = false;

static void print_banner() {
    Serial.println("\n");
    Serial.println("================================================================================");
    Serial.println("   MediBot Scale Subsystem - PURE RAW ADC HARDWARE & POWER TEST (NO WI-FI)");
    Serial.println("   Target: ESP-12F / ESP-12E | Pins: DOUT=GPIO4(D2), SCK=GPIO5(D1)");
    Serial.println("================================================================================");
}

void setup() {
    Serial.begin(115200);
    delay(1500);

    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH); // Off

    print_banner();

    // 1. COMPLETELY SHUT DOWN WI-FI MODEM & RF HARDWARE
    Serial.println("[RADIO] Shutting down 2.4 GHz Wi-Fi radio & ESP-NOW modem...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    delay(150);
    Serial.println("[RADIO] ✅ Wi-Fi Radio is completely OFF. Zero RF draw & zero Wi-Fi current pulses.");

    // 2. DIAGNOSTIC CHECK ON DOUT PIN BEFORE CLOCKING
    pinMode(PIN_HX711_DOUT, INPUT);
    int dout_state = digitalRead(PIN_HX711_DOUT);
    Serial.printf("[PIN DIAGNOSTIC] DOUT (GPIO %d) initial logic state: %s\n",
                  PIN_HX711_DOUT, dout_state ? "HIGH (3.3V)" : "LOW (GND)");
    if (dout_state == HIGH) {
        Serial.println("  ℹ️ Note: DOUT is HIGH. When idle/converting, HX711 holds DOUT HIGH until data is ready.");
    }

    // 3. INITIALIZE HX711 DRIVER
    Serial.printf("[HX711] Initializing on DOUT=GPIO%d, SCK=GPIO%d...\n", PIN_HX711_DOUT, PIN_HX711_SCK);
    scale.begin(PIN_HX711_DOUT, PIN_HX711_SCK);

    Serial.println("[HX711] Waiting up to 5 seconds for first conversion ready (DOUT -> LOW)...");
    uint32_t wait_start = millis();
    while (!scale.is_ready()) {
        delay(50);
        if (millis() - wait_start > 5000) {
            Serial.println("❌ ERROR: HX711 NOT RESPONDING (Timed out waiting for DOUT to go LOW).");
            Serial.println("   Possible causes:");
            Serial.println("   1. HX711 has NO power (Check VCC and GND with multimeter).");
            Serial.println("   2. DOUT and SCK wires are swapped (Verify DOUT is D2/GPIO4, SCK is D1/GPIO5).");
            Serial.println("   3. Loose breadboard jumper or poor solder joint.");
            Serial.println("   4. Faulty HX711 breakout board.");
            break;
        }
    }

    if (scale.is_ready()) {
        // Read initial baseline
        s_baseline_raw = scale.read();
        s_min_raw = s_baseline_raw;
        s_max_raw = s_baseline_raw;
        Serial.println("✅ SUCCESS: HX711 is alive, powered, and actively clocking data!");
        Serial.printf("   Zero / Baseline Raw ADC: %ld (0x%06lX)\n", s_baseline_raw, s_baseline_raw & 0xFFFFFF);
        Serial.println("\nCommands: [b/z] Reset baseline | [r] Reset Min/Max | [s] Toggle 100ms/300ms speed");
        Serial.println("--------------------------------------------------------------------------------");
        Serial.println("Sample # | Time (ms) | Raw ADC Value | Baseline Delta | Peak Spread | Deflection");
        Serial.println("--------------------------------------------------------------------------------");
    }
}

void loop() {
    // Process keyboard inputs from Serial Monitor
    if (Serial.available() > 0) {
        char c = Serial.read();
        if (c == 'b' || c == 'z') {
            if (scale.is_ready()) {
                s_baseline_raw = scale.read();
                Serial.printf("\n>>> [COMMAND] Baseline reset to: %ld\n\n", s_baseline_raw);
            }
        } else if (c == 'r') {
            s_min_raw = s_baseline_raw;
            s_max_raw = s_baseline_raw;
            Serial.println("\n>>> [COMMAND] Min/Max peak spread reset.\n");
        } else if (c == 's') {
            s_fast_mode = !s_fast_mode;
            Serial.printf("\n>>> [COMMAND] Stream speed: %s\n\n", s_fast_mode ? "FAST (100ms)" : "NORMAL (300ms)");
        }
    }

    if (scale.is_ready()) {
        // Blink LED briefly on valid sample read
        digitalWrite(PIN_STATUS_LED, LOW);

        long raw = scale.read();
        s_sample_count++;

        if (raw < s_min_raw) s_min_raw = raw;
        if (raw > s_max_raw) s_max_raw = raw;

        long delta = raw - s_baseline_raw;
        long spread = s_max_raw - s_min_raw;

        const char *deflection_tag = "No Load (Idle)";
        if (labs(delta) > 10000) {
            deflection_tag = "🚀 SIGNIFICANT DEFLECTION (>10k counts)";
        } else if (labs(delta) > 2000) {
            deflection_tag = "🔥 FIRM PRESSURE (>2k counts)";
        } else if (labs(delta) > 250) {
            deflection_tag = "⚠️ LIGHT TOUCH (>250 counts)";
        }

        Serial.printf("%8lu | %9lu | %13ld | %+14ld | %11ld | %s\n",
                      s_sample_count, millis(), raw, delta, spread, deflection_tag);

        digitalWrite(PIN_STATUS_LED, HIGH);
    } else {
        Serial.printf("%8lu | %9lu | [WAITING FOR ADC READY...] (DOUT is HIGH)\n", 
                      s_sample_count, millis());
    }

    delay(s_fast_mode ? 100 : 300);
}
