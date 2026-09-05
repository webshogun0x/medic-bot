/*
 * MediBot - Height & Weight Measurement Subsystem Firmware
 * Framework: ESP-IDF (FreeRTOS)
 * Target: ESP32-S3
 *
 * Hardware:
 *  - HX711 Load Cell Amplifier (Weight)
 *  - TB6600 Stepper Motor Driver (Vertical positioning)
 *  - HC-SR04 Ultrasonic Sensor (Height measurement)
 *  - Limit Switch (Homing & safety)
 *  - ESP-NOW Protocol (Wireless transmission to Main Controller)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "nvs_flash.h"

#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_now.h"

static const char *TAG = "HW_MODULE";

/* =========================================================================
 * Hardware Pin Definitions (ESP32-S3)
 * ========================================================================= */
#define PIN_HX711_DOUT          GPIO_NUM_4
#define PIN_HX711_SCK           GPIO_NUM_5

#define PIN_STEPPER_PUL         GPIO_NUM_6
#define PIN_STEPPER_DIR         GPIO_NUM_7
#define PIN_STEPPER_ENA         GPIO_NUM_8

#define PIN_ULTRASONIC_TRIG     GPIO_NUM_9
#define PIN_ULTRASONIC_ECHO     GPIO_NUM_10

#define PIN_LIMIT_SWITCH        GPIO_NUM_11

/* =========================================================================
 * Calibration & System Geometry Constants
 * ========================================================================= */
#define HOME_POSITION_CM        150.0f
#define SENSOR_MOUNT_HEIGHT_CM  195.0f
#define WEIGHT_DETECT_THRESH_KG 20.0f   // Lowered from 50kg to support light/young patients
#define WEIGHT_STEP_OFF_KG      10.0f

#define HX711_CALIBRATION_FACTOR (-7050.0f)

// Motor parameters (NEMA 24, 200 steps/rev, 8 microsteps, 8mm pitch lead screw)
#define STEPS_PER_REV           200
#define MICROSTEPS              8
#define LEAD_SCREW_PITCH_MM     8.0f
#define STEPS_PER_MM            ((float)(STEPS_PER_REV * MICROSTEPS) / LEAD_SCREW_PITCH_MM) // 200 steps/mm
#define MAX_TRAVEL_CM           50.0f
#define MOTOR_STEP_DELAY_US     800
#define HOMING_STEP_DELAY_US    1200
#define CLOCKWISE_IS_UP         1

/* =========================================================================
 * Data Structures
 * ========================================================================= */
typedef struct __attribute__((packed)) {
    float weight_kg;
    float height_sonar_cm;
    float height_lidar_cm;
    float bmi_sonar;
    float bmi_lidar;
    uint32_t timestamp;
} hw_module_packet_t;

static hw_module_packet_t s_latest_measurement = {0};
static uint8_t s_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static long s_hx711_offset = 0;
static float s_current_position_cm = HOME_POSITION_CM;
static bool s_is_homed = false;

/* =========================================================================
 * HX711 Driver (Weight Sensing)
 * ========================================================================= */
static void hx711_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_HX711_DOUT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1ULL << PIN_HX711_SCK);
    io_conf.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io_conf);
    gpio_set_level(PIN_HX711_SCK, 0);
}

static bool hx711_is_ready(void) {
    return gpio_get_level(PIN_HX711_DOUT) == 0;
}

static long hx711_read_raw(void) {
    int timeout_ms = 500;
    while (!hx711_is_ready() && timeout_ms-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (!hx711_is_ready()) {
        return 0;
    }

    uint32_t count = 0;
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);

    for (int i = 0; i < 24; i++) {
        gpio_set_level(PIN_HX711_SCK, 1);
        esp_rom_delay_us(1);
        count = (count << 1);
        gpio_set_level(PIN_HX711_SCK, 0);
        esp_rom_delay_us(1);
        if (gpio_get_level(PIN_HX711_DOUT)) {
            count++;
        }
    }

    // 25th pulse for Gain 128 (Channel A)
    gpio_set_level(PIN_HX711_SCK, 1);
    esp_rom_delay_us(1);
    gpio_set_level(PIN_HX711_SCK, 0);
    esp_rom_delay_us(1);

    portEXIT_CRITICAL(&mux);

    // Sign extend 24-bit value to 32-bit signed long
    if (count & 0x800000) {
        count |= 0xFF000000;
    }

    return (long)count;
}

static void hx711_tare(int samples) {
    long sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += hx711_read_raw();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_hx711_offset = sum / samples;
    ESP_LOGI(TAG, "HX711 tared. Offset: %ld", s_hx711_offset);
}

static float hx711_get_weight_kg(int samples) {
    long sum = 0;
    int valid = 0;
    for (int i = 0; i < samples; i++) {
        long raw = hx711_read_raw();
        if (raw != 0) {
            sum += raw;
            valid++;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    if (valid == 0) return 0.0f;
    long avg = sum / valid;
    float weight = (float)(avg - s_hx711_offset) / HX711_CALIBRATION_FACTOR;
    return (weight < 0.0f) ? 0.0f : weight;
}

/* =========================================================================
 * HC-SR04 Ultrasonic Driver (Height Sensing)
 * ========================================================================= */
static void ultrasonic_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_ULTRASONIC_TRIG),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PIN_ULTRASONIC_TRIG, 0);

    io_conf.pin_bit_mask = (1ULL << PIN_ULTRASONIC_ECHO);
    io_conf.mode = GPIO_MODE_INPUT;
    gpio_config(&io_conf);
}

static float ultrasonic_measure_distance_cm(void) {
    gpio_set_level(PIN_ULTRASONIC_TRIG, 0);
    esp_rom_delay_us(2);
    gpio_set_level(PIN_ULTRASONIC_TRIG, 1);
    esp_rom_delay_us(10);
    gpio_set_level(PIN_ULTRASONIC_TRIG, 0);

    int64_t start_wait = esp_timer_get_time();
    while (gpio_get_level(PIN_ULTRASONIC_ECHO) == 0) {
        if (esp_timer_get_time() - start_wait > 30000) { // 30ms timeout
            return -1.0f;
        }
    }

    int64_t echo_start = esp_timer_get_time();
    while (gpio_get_level(PIN_ULTRASONIC_ECHO) == 1) {
        if (esp_timer_get_time() - echo_start > 30000) { // 30ms timeout (~5 meters max)
            break;
        }
    }
    int64_t echo_end = esp_timer_get_time();

    int64_t duration_us = echo_end - echo_start;
    float distance_cm = (float)duration_us * 0.0343f / 2.0f;
    return (distance_cm > 2.0f && distance_cm < 250.0f) ? distance_cm : -1.0f;
}

static float ultrasonic_get_averaged_height(void) {
    float sum = 0.0f;
    int valid = 0;
    for (int i = 0; i < 5; i++) {
        float dist = ultrasonic_measure_distance_cm();
        if (dist > 0.0f) {
            sum += dist;
            valid++;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (valid > 0) {
        float avg_dist = sum / (float)valid;
        float height = SENSOR_MOUNT_HEIGHT_CM - avg_dist;
        ESP_LOGI(TAG, "Ultrasonic head distance: %.1f cm -> Person Height: %.1f cm", avg_dist, height);
        return (height > 50.0f && height < 230.0f) ? height : 0.0f;
    }
    return 0.0f;
}

/* =========================================================================
 * Stepper Motor & Homing Driver
 * ========================================================================= */
static void stepper_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_STEPPER_PUL) | (1ULL << PIN_STEPPER_DIR) | (1ULL << PIN_STEPPER_ENA),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PIN_STEPPER_ENA, 0); // LOW = Enabled
    gpio_set_level(PIN_STEPPER_PUL, 0);
    gpio_set_level(PIN_STEPPER_DIR, 0);

    // Limit switch with internal pullup
    io_conf.pin_bit_mask = (1ULL << PIN_LIMIT_SWITCH);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
}

static void stepper_home(void) {
    ESP_LOGI(TAG, "Homing stepper motor...");
    if (gpio_get_level(PIN_LIMIT_SWITCH) == 0) {
        ESP_LOGI(TAG, "Already at home limit switch");
        s_current_position_cm = HOME_POSITION_CM;
        s_is_homed = true;
        return;
    }

    gpio_set_level(PIN_STEPPER_DIR, CLOCKWISE_IS_UP ? 0 : 1); // Move DOWN
    int max_steps = (int)(MAX_TRAVEL_CM * 10.0f * STEPS_PER_MM * 1.5f);
    int steps = 0;

    while (gpio_get_level(PIN_LIMIT_SWITCH) != 0 && steps < max_steps) {
        gpio_set_level(PIN_STEPPER_PUL, 1);
        esp_rom_delay_us(HOMING_STEP_DELAY_US);
        gpio_set_level(PIN_STEPPER_PUL, 0);
        esp_rom_delay_us(HOMING_STEP_DELAY_US);
        steps++;

        if (steps % 200 == 0) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    if (gpio_get_level(PIN_LIMIT_SWITCH) == 0) {
        ESP_LOGI(TAG, "Home reached successfully");
        s_current_position_cm = HOME_POSITION_CM;
        s_is_homed = true;
    } else {
        ESP_LOGW(TAG, "Homing timed out or limit switch not triggered");
        s_is_homed = false;
    }
}

static void stepper_move_cm(float distance_cm, bool move_up) {
    if (distance_cm <= 0.0f) return;
    long steps = (long)(distance_cm * 10.0f * STEPS_PER_MM);

    bool dir_val = move_up ? (CLOCKWISE_IS_UP == 1) : (CLOCKWISE_IS_UP == 0);
    gpio_set_level(PIN_STEPPER_DIR, dir_val ? 1 : 0);

    for (long i = 0; i < steps; i++) {
        if (!move_up && gpio_get_level(PIN_LIMIT_SWITCH) == 0) {
            ESP_LOGW(TAG, "Emergency stop: limit switch touched during downward travel");
            s_current_position_cm = HOME_POSITION_CM;
            break;
        }
        gpio_set_level(PIN_STEPPER_PUL, 1);
        esp_rom_delay_us(MOTOR_STEP_DELAY_US);
        gpio_set_level(PIN_STEPPER_PUL, 0);
        esp_rom_delay_us(MOTOR_STEP_DELAY_US);

        if (i % 200 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    s_current_position_cm += move_up ? distance_cm : -distance_cm;
    ESP_LOGI(TAG, "Stepper new position: %.1f cm", s_current_position_cm);
}

/* =========================================================================
 * ESP-NOW Communications
 * ========================================================================= */
static void espnow_send_measurement(void) {
    esp_err_t res = esp_now_send(s_broadcast_mac, (uint8_t *)&s_latest_measurement, sizeof(hw_module_packet_t));
    if (res == ESP_OK) {
        ESP_LOGI(TAG, "ESP-NOW broadcast sent: W=%.1fkg, H=%.1fcm, BMI=%.1f",
                 s_latest_measurement.weight_kg,
                 s_latest_measurement.height_sonar_cm,
                 s_latest_measurement.bmi_sonar);
    } else {
        ESP_LOGW(TAG, "ESP-NOW send failed: %s", esp_err_to_name(res));
    }
}

static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len > 0 && data[0] == 0xAA) {
        ESP_LOGI(TAG, "Data request command (0xAA) received from Main Controller!");
        espnow_send_measurement();
    }
}

static void espnow_init_module(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Lock to Channel 1 (or sync with Main Controller)
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, s_broadcast_mac, 6);
    peer.channel = 1;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "ESP-NOW ready on Channel 1. MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* =========================================================================
 * Main Application Task
 * ========================================================================= */
static void measurement_loop_task(void *pvParameters) {
    ESP_LOGI(TAG, "Measurement loop task running. Waiting for user on scale...");

    while (1) {
        float weight = hx711_get_weight_kg(5);

        if (weight >= WEIGHT_DETECT_THRESH_KG) {
            ESP_LOGI(TAG, "User detected on scale (Weight: %.1f kg). Waiting 3s to stabilize...", weight);
            vTaskDelay(pdMS_TO_TICKS(3000));

            // Measure stable weight
            weight = hx711_get_weight_kg(10);
            s_latest_measurement.weight_kg = weight;

            // Measure height
            float height_cm = ultrasonic_get_averaged_height();
            s_latest_measurement.height_sonar_cm = height_cm;
            s_latest_measurement.height_lidar_cm = height_cm; // Fallback to sonar if LiDAR not populated

            // Calculate BMI
            if (height_cm > 50.0f) {
                float height_m = height_cm / 100.0f;
                float bmi = weight / (height_m * height_m);
                s_latest_measurement.bmi_sonar = bmi;
                s_latest_measurement.bmi_lidar = bmi;
            } else {
                s_latest_measurement.bmi_sonar = 0.0f;
                s_latest_measurement.bmi_lidar = 0.0f;
            }

            s_latest_measurement.timestamp = (uint32_t)(esp_timer_get_time() / 1000000ULL);

            // Adjust motorized head carriage if homed
            if (s_is_homed && height_cm > 50.0f) {
                float delta = height_cm - s_current_position_cm;
                if (fabsf(delta) > 2.0f) {
                    stepper_move_cm(fabsf(delta), delta > 0.0f);
                }
            }

            // Transmit results immediately via ESP-NOW
            espnow_send_measurement();

            // Wait for user to step off
            ESP_LOGI(TAG, "Measurement complete. Waiting for patient to step off...");
            while (hx711_get_weight_kg(5) > WEIGHT_STEP_OFF_KG) {
                vTaskDelay(pdMS_TO_TICKS(500));
            }

            ESP_LOGI(TAG, "Patient stepped off. Resetting carriage to home position...");
            stepper_home();
            ESP_LOGI(TAG, "Ready for next patient.");
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== MEDIC-BOT HEIGHT & WEIGHT MODULE (ESP-IDF) STARTING ===");

    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize peripherals
    hx711_init();
    ultrasonic_init();
    stepper_init();

    // 3. Tare scale
    hx711_tare(20);

    // 4. Initial homing of carriage
    stepper_home();

    // 5. Initialize ESP-NOW
    espnow_init_module();

    // 6. Launch measurement loop task
    xTaskCreatePinnedToCore(measurement_loop_task, "meas_task", 4096, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "Initialization complete.");
}
