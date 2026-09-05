#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "user_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SD Card over SPI3 (HSPI), mount FAT filesystem,
 * initialize SQLite3 tables (USERS, READINGS) with indexes,
 * and launch the background storage FreeRTOS task.
 * @return ESP_OK on success.
 */
esp_err_t storage_manager_init(void);

/**
 * @brief Synchronous helper: check if user exists in local DB.
 * @param rfid RFID tag UID string.
 * @param out_user Pointer to receive loaded profile.
 * @return true if user found, false otherwise.
 */
bool storage_manager_get_user(const char *rfid, user_profile_t *out_user);

/**
 * @brief Synchronous helper: save or update user in local DB using parameterized SQL.
 * @param user User profile to save.
 * @return true on success.
 */
bool storage_manager_save_user(const user_profile_t *user);

/**
 * @brief Synchronous helper: save vital signs reading in local DB using parameterized SQL.
 * @param reading Health reading to record.
 * @return true on success.
 */
bool storage_manager_save_reading(const vital_readings_t *reading);

/**
 * @brief Update fingerprint registration status for user.
 * @param rfid RFID tag UID.
 * @param fp_id Fingerprint slot number.
 * @return true on success.
 */
bool storage_manager_update_fingerprint(const char *rfid, uint8_t fp_id);

/**
 * @brief Check if SD card and database are healthy.
 */
bool storage_manager_is_healthy(void);

#ifdef __cplusplus
}
#endif
