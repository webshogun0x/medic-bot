#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "user_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize ESP-NOW protocol and register RX/TX callbacks.
 * Must be called after wifi_manager_init().
 * @return ESP_OK on success.
 */
esp_err_t espnow_manager_init(void);

/**
 * @brief Send request command (0xAA) to the Height/Weight module.
 * @return ESP_OK on success.
 */
esp_err_t espnow_manager_request_data(void);

/**
 * @brief Set the target MAC address for the Height/Weight module peer.
 * @param mac 6-byte array containing target MAC address.
 * @return ESP_OK on success.
 */
esp_err_t espnow_manager_set_peer(const uint8_t *mac);

#ifdef __cplusplus
}
#endif
