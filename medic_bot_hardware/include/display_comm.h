#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "user_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char data[256];
} display_msg_t;

/**
 * @brief Initialize Display UART driver and launch TX/RX FreeRTOS tasks.
 * @return ESP_OK on success.
 */
esp_err_t display_comm_init(void);

/**
 * @brief Send raw string to display (thread-safe, queues to display TX task).
 */
void display_send_raw(const char *format, ...);

/**
 * @brief Send standard UI prompt text.
 */
void display_send_prompt(const char *message);

/**
 * @brief Send system connectivity status to display.
 */
void display_send_status(const char *message, bool wifi_ok, const char *ip_str, bool firebase_ok);

/**
 * @brief Send user profile payload for dashboard display.
 */
void display_send_user_data(const user_profile_t *user);

/**
 * @brief Send live vital signs & BMI payload.
 */
void display_send_sensor_data(const vital_readings_t *vitals);

/**
 * @brief Send simple typed message (e.g. FINGERPRINT_SUCCESS, FINGERPRINT_ERROR).
 */
void display_send_typed(const char *msg_type, const char *message);

#ifdef __cplusplus
}
#endif
