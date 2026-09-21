#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "user_types.h"
#include "espnow_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EVT_NONE = 0,
    /* --- Display UI commands --- */
    EVT_DISPLAY_READY,
    EVT_CMD_START_LOGIN,
    EVT_CMD_START_ENROLLMENT,
    EVT_CMD_READ_OXIMETER,
    EVT_CMD_SAVE_READINGS,
    EVT_CMD_BACK,
    EVT_CMD_LOGOUT,

    /* --- Biometric & RFID sensors --- */
    EVT_RFID_SCANNED,
    EVT_FP_MATCH_SUCCESS,
    EVT_FP_MATCH_FAILED,
    EVT_FP_ENROLL_STEP_OK,
    EVT_FP_ENROLL_DONE,
    EVT_FP_ENROLL_FAILED,
    EVT_FP_NO_FINGER,
    EVT_OXIMETER_DONE,
    EVT_OXIMETER_NO_FINGER,

    /* --- ESP-NOW distributed peripheral nodes --- */
    EVT_ESPNOW_WEIGHT_READY,
    EVT_ESPNOW_HEIGHT_READY,
    EVT_ESPNOW_STEPPER_ACK,
    EVT_ESPNOW_TEMP_READY,

    /* --- Network & Cloud --- */
    EVT_WIFI_CONNECTED,
    EVT_WIFI_DISCONNECTED,
    EVT_FIREBASE_READY,
    EVT_FIREBASE_FAILED,
    EVT_CLOUD_SYNC_DONE,

    /* --- Storage --- */
    EVT_USER_LOADED,
    EVT_USER_NOT_FOUND,
    EVT_READINGS_SAVED,
} sys_event_type_t;

typedef struct {
    sys_event_type_t type;
    union {
        char rfid[32];
        char rfid_uid[32];
        uint8_t fp_id;
        int fingerprint_slot;
        struct {
            float hr;
            float spo2;
            float temperature;
        } oximeter;
        struct {
            uint8_t sender_node;
            float val;
            uint8_t status;
        } espnow_node_data;
        espnow_kiosk_packet_t espnow_pkt;
        user_profile_t user;
        vital_readings_t vitals;
        struct {
            bool connected;
            char ip[16];
        } wifi_info;
    } payload;
} sys_event_t;

/* Storage command structures */
typedef enum {
    STORE_CMD_LOAD_USER,
    STORE_CMD_SAVE_USER,
    STORE_CMD_SAVE_READING,
    STORE_CMD_UPDATE_FP,
    STORE_CMD_MARK_SYNCED,
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
        struct {
            char rfid[32];
            char timestamp[32];
        } sync_mark;
    } data;
} storage_cmd_t;

/* Cloud command structures */
typedef enum {
    CLOUD_CMD_SYNC_READING,
    CLOUD_CMD_FETCH_USER,
    CLOUD_CMD_UPDATE_FP_STATUS,
} cloud_cmd_type_t;

typedef struct {
    cloud_cmd_type_t type;
    union {
        char rfid[32];
        vital_readings_t reading;
        struct {
            char rfid[32];
            uint8_t fp_id;
            bool registered;
        } fp_status;
    } data;
} cloud_cmd_t;

// Global FreeRTOS queues
extern QueueHandle_t g_sys_event_queue;
extern QueueHandle_t g_display_tx_queue;
extern QueueHandle_t g_storage_queue;
extern QueueHandle_t g_cloud_queue;

#ifdef __cplusplus
}
#endif
