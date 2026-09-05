#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "user_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Cloud Sync task for Firebase Realtime Database REST API.
 * Runs on Core 0 and processes requests from g_cloud_queue.
 * @return ESP_OK on success.
 */
esp_err_t cloud_sync_init(void);

/**
 * @brief Queue a vital signs reading to be synced to Firebase asynchronously.
 * @param reading Pointer to reading struct.
 * @return ESP_OK if successfully queued.
 */
esp_err_t cloud_sync_queue_reading(const vital_readings_t *reading);

/**
 * @brief Queue a request to fetch user profile from Firebase cloud database.
 * Responds with EVT_USER_LOADED or EVT_USER_NOT_FOUND on g_sys_event_queue.
 * @param rfid RFID tag UID string.
 * @return ESP_OK if successfully queued.
 */
esp_err_t cloud_sync_queue_fetch_user(const char *rfid);

/**
 * @brief Queue a request to update fingerprint registration status on Firebase.
 * @param rfid RFID tag UID.
 * @param fp_id Fingerprint slot ID.
 * @return ESP_OK if successfully queued.
 */
esp_err_t cloud_sync_queue_update_fp(const char *rfid, uint8_t fp_id);

/**
 * @brief Check if Cloud Sync client is ready.
 */
bool cloud_sync_is_ready(void);

#ifdef __cplusplus
}
#endif
