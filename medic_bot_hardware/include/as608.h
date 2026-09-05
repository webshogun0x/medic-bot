#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// AS608 Confirmation codes
#define AS608_OK                    0x00
#define AS608_PACKET_ERROR          0x01
#define AS608_NO_FINGER             0x02
#define AS608_FAIL_TO_ENROLL        0x03
#define AS608_BAD_IMAGE             0x06
#define AS608_FEAT_TOO_FEW          0x07
#define AS608_NOT_MATCH             0x08
#define AS608_NOT_FOUND             0x09
#define AS608_ENROLL_MISMATCH       0x0A
#define AS608_BAD_LOCATION          0x0B
#define AS608_FLASH_ERROR           0x18
#define AS608_COMM_TIMEOUT          0xFE
#define AS608_COMM_ERROR            0xFF

/**
 * @brief Initialize AS608 UART driver.
 * @return ESP_OK on success.
 */
esp_err_t as608_init(void);

/**
 * @brief Capture finger image from optical sensor.
 * @return AS608 confirmation code (AS608_OK, AS608_NO_FINGER, etc.).
 */
uint8_t as608_get_image(void);

/**
 * @brief Convert raw image into character template.
 * @param buffer_id 1 for CharBuffer1, 2 for CharBuffer2.
 * @return AS608 confirmation code.
 */
uint8_t as608_image_to_template(uint8_t buffer_id);

/**
 * @brief Combine CharBuffer1 and CharBuffer2 to generate template model.
 * @return AS608 confirmation code.
 */
uint8_t as608_create_model(void);

/**
 * @brief Store template model to flash slot.
 * @param buffer_id Buffer containing model (usually 1 or 2).
 * @param slot_id Slot number (1 to 127).
 * @return AS608 confirmation code.
 */
uint8_t as608_store_model(uint8_t buffer_id, uint16_t slot_id);

/**
 * @brief Fast search flash library for matching fingerprint.
 * @param buffer_id Template buffer to search with (usually 1).
 * @param matched_id Pointer to receive matched slot ID.
 * @param score Pointer to receive confidence score.
 * @return AS608 confirmation code (AS608_OK on match).
 */
uint8_t as608_fast_search(uint8_t buffer_id, uint16_t *matched_id, uint16_t *score);

#ifdef __cplusplus
}
#endif
