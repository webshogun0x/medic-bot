#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi station mode and start connection attempts.
 * Also initializes SNTP for network time synchronization.
 * @param ssid WiFi network SSID (can be NULL to use NVS/default).
 * @param password WiFi network password.
 * @return ESP_OK on success.
 */
esp_err_t wifi_manager_init(const char *ssid, const char *password);

/**
 * @brief Check if WiFi is currently connected and has acquired an IP address.
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Get the current IPv4 address string.
 * @param buf Output buffer for IP string.
 * @param max_len Size of buffer (minimum 16 bytes).
 */
void wifi_manager_get_ip(char *buf, size_t max_len);

/**
 * @brief Get the current primary WiFi channel (used to sync ESP-NOW).
 * @param channel Pointer to uint8_t to store channel.
 * @return ESP_OK on success.
 */
esp_err_t wifi_manager_get_channel(uint8_t *channel);

#ifdef __cplusplus
}
#endif
