#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize I2C0 bus and MAX30102 sensor registers.
 * @return ESP_OK if sensor detected and configured.
 */
esp_err_t max30102_init(void);

/**
 * @brief Trigger an asynchronous, non-blocking oximeter measurement task.
 * The task will read samples, compute HR and SpO2, and post EVT_OXIMETER_DONE
 * or EVT_OXIMETER_NO_FINGER to g_sys_event_queue.
 * @return ESP_OK if task was started.
 */
esp_err_t max30102_start_measurement(void);

/**
 * @brief Check if an oximeter measurement is currently in progress.
 */
bool max30102_is_busy(void);

/**
 * @brief Cancel any running oximeter measurement task.
 */
void max30102_cancel_measurement(void);

/**
 * @brief Read onboard die temperature in Celsius.
 * @param temp_c Pointer to float to store temperature.
 * @return ESP_OK on success.
 */
esp_err_t max30102_read_temperature(float *temp_c);

#ifdef __cplusplus
}
#endif
