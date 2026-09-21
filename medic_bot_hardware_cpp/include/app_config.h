#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Hardware Pin Assignments (ESP32-S3 — from COMPLETE_WIRING_DIAGRAM.md)
 * ========================================================================= */

// Motor & Actuator Control
#define PIN_MOTOR_IN1           GPIO_NUM_1
#define PIN_MOTOR_IN2           GPIO_NUM_2
#define PIN_MOTOR_RELAY         GPIO_NUM_30
#define PIN_BUZZER              GPIO_NUM_42

// Display Unit UART (to Sunton 7" ESP32-S3 LCD)
#define DISPLAY_UART_NUM        UART_NUM_2
#define PIN_DISPLAY_TX          GPIO_NUM_6
#define PIN_DISPLAY_RX          GPIO_NUM_7
#define DISPLAY_BAUDRATE        115200
#define DISPLAY_RX_BUF_SIZE     1024
#define DISPLAY_TX_BUF_SIZE     1024

// MAX30102 Pulse Oximeter (I2C Bus 0)
#define I2C_MASTER_NUM          0
#define PIN_I2C_SDA             GPIO_NUM_8
#define PIN_I2C_SCL             GPIO_NUM_9
#define I2C_MASTER_FREQ_HZ      400000
#define MAX30102_I2C_ADDR       0x57

// SD Card Module (SPI3 / HSPI Bus - Pins: 1, 2, 4, 5)
#define SD_SPI_HOST             SPI3_HOST
#define PIN_SD_CS               GPIO_NUM_1
#define PIN_SD_MOSI             GPIO_NUM_2
#define PIN_SD_SCK              GPIO_NUM_4
#define PIN_SD_MISO             GPIO_NUM_5
#define SD_MOUNT_POINT          "/sdcard"
#define SD_DB_PATH              "/sdcard/medibot.db"
#define SD_USERS_DIR            "/sdcard/users"
#define SD_READINGS_DIR         "/sdcard/readings"

// AS608 Fingerprint Sensor (UART1 - Pins: 17, 18)
#define FP_UART_NUM             UART_NUM_1
#define PIN_FP_TX               GPIO_NUM_17
#define PIN_FP_RX               GPIO_NUM_18
#define FP_BAUDRATE             57600
#define FP_RX_BUF_SIZE          512

// MFRC522 RFID Reader (SPI2 / FSPI Bus - Pins: 47, 21, 11, 12, 13)
#define RFID_SPI_HOST           SPI2_HOST
#define PIN_RC522_MOSI          GPIO_NUM_11
#define PIN_RC522_MISO          GPIO_NUM_12
#define PIN_RC522_SCK           GPIO_NUM_13
#define PIN_RC522_CS            GPIO_NUM_21
#define PIN_RC522_RST           GPIO_NUM_47

// Voice Guidance MP3 Module (GPIO trigger)
#define PIN_VOICE_NEXT          GPIO_NUM_10
#define PIN_VOICE_PLAY_PAUSE    GPIO_NUM_46
#define PIN_VOICE_RESET         GPIO_NUM_48
#define VOICE_PULSE_MS          150
#define VOICE_TRACK_MAX_MS      9500
#define VOICE_RESET_BOOT_MS     10000

/* =========================================================================
 * Network Configuration
 * ========================================================================= */
#define WIFI_SSID_DEFAULT       "MEDICBOT_AP"
#define WIFI_PASS_DEFAULT       "medicbot123"
#define WIFI_SSID_MAX_LEN       32
#define WIFI_PASS_MAX_LEN       64
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_RECONNECT_DELAY_MS 10000

#define NTP_SERVER              "pool.ntp.org"
#define NTP_TZ                  "WAT-1"          // West Africa Time UTC+1

// Firebase REST API defaults
#define FIREBASE_HOST_DEFAULT   "https://medic-bot-health-monitor-default-rtdb.firebaseio.com"
#define FIREBASE_API_KEY_DEF    "mewFLH4S8st5ZVg9iJwMuWcmQ3eY4tbj2CBicozG"
#define FIREBASE_EMAIL_DEF      ""
#define FIREBASE_PASS_DEF       ""

/* =========================================================================
 * ESP-NOW Configuration
 * ========================================================================= */
#define ESPNOW_CHANNEL          0                // 0 = use current WiFi channel

/* =========================================================================
 * FreeRTOS Task Configuration & Core Pinning
 * ========================================================================= */
#define CORE_NETWORK_STORAGE    0   // Core 0: WiFi stack, Firebase, ESP-NOW, SD I/O
#define CORE_REALTIME_APP       1   // Core 1: FSM Orchestrator, Display, Sensors, Auth
#define CORE_SYS_NETWORK        0   // Alias

// Task priorities (higher = more important)
#define PRIO_DISPLAY_COMM       5
#define PRIO_ORCHESTRATOR       4
#define PRIO_APP_FSM            4
#define PRIO_RFID_POLL          3
#define PRIO_FINGERPRINT        3
#define PRIO_OXIMETER           3
#define PRIO_ESPNOW             4
#define PRIO_STORAGE            2
#define PRIO_CLOUD_SYNC         1
#define PRIO_VOICE              2

// Stack sizes (bytes)
#define STACK_FSM               8192
#define STACK_DISPLAY           4096
#define STACK_RFID              3072
#define STACK_FINGERPRINT       4096
#define STACK_OXIMETER          4096
#define STACK_STORAGE           8192
#define STACK_CLOUD             8192
#define STACK_WIFI              4096
#define STACK_ESPNOW            4096
#define STACK_VOICE             2048

// Queue depths
#define QUEUE_SYS_EVENT_DEPTH   16
#define QUEUE_DISPLAY_TX_DEPTH  16
#define QUEUE_STORAGE_DEPTH     8
#define QUEUE_CLOUD_DEPTH       8

/* =========================================================================
 * Measurement / System Constants
 * ========================================================================= */
#define HEIGHT_REF_METERS       2.20f
#define FP_MAX_SLOTS            127
#define CLOUD_SYNC_INTERVAL_MS  60000
#define RFID_POLL_INTERVAL_MS   200

#ifdef __cplusplus
}
#endif
