#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "user_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * System States (Finite State Machine)
 * ========================================================================= */
typedef enum {
    SYS_STATE_INIT,
    SYS_STATE_IDLE,
    SYS_STATE_LOGIN_WAIT_RFID,
    SYS_STATE_LOGIN_WAIT_FP,
    SYS_STATE_ENROLL_WAIT_RFID,
    SYS_STATE_ENROLL_SCAN_FP1,
    SYS_STATE_ENROLL_WAIT_REMOVE,
    SYS_STATE_ENROLL_SCAN_FP2,
    SYS_STATE_DASHBOARD,
    SYS_STATE_MEASURING,
} system_state_t;

/* =========================================================================
 * System Event Types — every inter-task notification is an event
 * ========================================================================= */
typedef enum {
    /* --- Display commands (from Display RX task) --- */
    EVT_DISPLAY_READY,
    EVT_CMD_START_LOGIN,
    EVT_CMD_START_ENROLLMENT,
    EVT_CMD_READ_OXIMETER,
    EVT_CMD_SAVE_READINGS,
    EVT_CMD_BACK,
    EVT_CMD_LOGOUT,

    /* --- Sensor results --- */
    EVT_RFID_SCANNED,           // payload.rfid[]
    EVT_FP_MATCH_SUCCESS,       // payload.fp_id
    EVT_FP_MATCH_FAILED,
    EVT_FP_ENROLL_STEP_OK,      // first scan captured
    EVT_FP_ENROLL_DONE,         // enrollment complete, payload.fp_id
    EVT_FP_ENROLL_FAILED,
    EVT_FP_NO_FINGER,           // finger removed (used during enrollment)
    EVT_OXIMETER_DONE,          // payload.oximeter
    EVT_OXIMETER_NO_FINGER,
    EVT_HW_DATA_RECEIVED,       // payload.hw_data (from height/weight module)

    /* --- Network status --- */
    EVT_WIFI_CONNECTED,         // payload.wifi_info
    EVT_WIFI_DISCONNECTED,
    EVT_FIREBASE_READY,
    EVT_FIREBASE_FAILED,
    EVT_CLOUD_SYNC_DONE,

    /* --- Storage results (response from storage task) --- */
    EVT_USER_LOADED,            // payload.user
    EVT_USER_NOT_FOUND,
    EVT_READINGS_SAVED,
} sys_event_type_t;

/* =========================================================================
 * System Event Payload — union keeps it compact on the queue
 * ========================================================================= */
typedef struct {
    sys_event_type_t type;
    union {
        char rfid[32];
        uint8_t fp_id;
        struct {
            float hr;
            float spo2;
            float temperature;
        } oximeter;
        hw_module_packet_t hw_data;
        user_profile_t user;
        vital_readings_t vitals;
        struct {
            bool connected;
            char ip[16];
        } wifi_info;
    } payload;
} sys_event_t;

/* =========================================================================
 * Storage Task Commands — queued to g_storage_queue
 * ========================================================================= */
typedef enum {
    STORE_CMD_LOAD_USER,        // data.rfid -> responds EVT_USER_LOADED / NOT_FOUND
    STORE_CMD_SAVE_USER,        // data.user
    STORE_CMD_SAVE_READING,     // data.reading -> responds EVT_READINGS_SAVED
    STORE_CMD_UPDATE_FP,        // data.fp_update
    STORE_CMD_MARK_SYNCED,      // data.rfid + timestamp
} storage_cmd_type_t;

typedef struct {
    storage_cmd_type_t type;
    union {
        char rfid[32];
        user_profile_t user;
        vital_readings_t reading;
        struct {
            char rfid[32];
            uint8_t fp_id;
        } fp_update;
    } data;
} storage_cmd_t;

/* =========================================================================
 * Cloud Sync Commands — queued to g_cloud_queue
 * ========================================================================= */
typedef enum {
    CLOUD_CMD_SYNC_READING,     // data.reading
    CLOUD_CMD_FETCH_USER,       // data.rfid -> responds EVT_USER_LOADED / NOT_FOUND
    CLOUD_CMD_UPDATE_FP_STATUS, // data.fp_status
} cloud_cmd_type_t;

typedef struct {
    cloud_cmd_type_t type;
    union {
        vital_readings_t reading;
        char rfid[32];
        struct {
            char rfid[32];
            uint8_t fp_id;
            bool registered;
        } fp_status;
    } data;
} cloud_cmd_t;

/* =========================================================================
 * Global FreeRTOS Primitives — created in main.c, externed here
 * ========================================================================= */

// Inter-task communication queues
extern QueueHandle_t  g_sys_event_queue;    // sys_event_t items
extern QueueHandle_t  g_display_tx_queue;   // display_msg_t items
extern QueueHandle_t  g_storage_queue;      // storage_cmd_t items
extern QueueHandle_t  g_cloud_queue;        // cloud_cmd_t items

// Bus protection mutexes
extern SemaphoreHandle_t g_spi2_mutex;      // RFID SPI bus
extern SemaphoreHandle_t g_spi3_mutex;      // SD card SPI bus
extern SemaphoreHandle_t g_i2c_mutex;       // I2C bus (MAX30102)

// Shared application state (protected by g_state_mutex)
extern SemaphoreHandle_t g_state_mutex;
extern system_state_t    g_current_state;
extern user_profile_t    g_current_user;
extern vital_readings_t  g_current_vitals;
extern bool              g_wifi_connected;
extern bool              g_firebase_ready;

#ifdef __cplusplus
}
#endif
