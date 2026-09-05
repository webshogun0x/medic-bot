#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MFRC522 RFID reader over SPI2.
 * @return ESP_OK on success, ESP_FAIL if module not responding.
 */
esp_err_t mfrc522_init(void);

/**
 * @brief Check if a card/tag is present and read its UID (non-blocking).
 * @param uid_out Buffer to receive uppercase hex UID (e.g. "A1B2C3D4").
 * @param max_len Size of buffer (minimum 16 bytes recommended).
 * @return true if card was detected and read, false otherwise.
 */
bool mfrc522_read_card(char *uid_out, size_t max_len);

/**
 * @brief Put detected card into halt state to allow subsequent actions.
 */
void mfrc522_halt(void);

#ifdef __cplusplus
}
#endif
