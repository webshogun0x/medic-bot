#include "display_comm.h"
#include "app_config.h"
#include "system_events.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static const char *TAG = "DISPLAY_COMM";
static QueueHandle_t s_uart_event_queue = NULL;

static void display_rx_task(void *pvParameters) {
    uint8_t rx_byte = 0;
    char line_buf[128];
    uint8_t line_len = 0;

    ESP_LOGI(TAG, "Display RX Task started");

    while (1) {
        // Read 1 byte with 50ms timeout to keep task responsive
        int len = uart_read_bytes(DISPLAY_UART_NUM, &rx_byte, 1, pdMS_TO_TICKS(50));
        if (len <= 0) {
            continue;
        }

        if (rx_byte == '\n' || rx_byte == '\r') {
            if (line_len > 0) {
                line_buf[line_len] = '\0';
                ESP_LOGI(TAG, "Display RX: [%s]", line_buf);

                sys_event_t evt;
                memset(&evt, 0, sizeof(evt));
                bool valid_cmd = false;

                if (strstr(line_buf, "DISPLAY_READY")) {
                    evt.type = EVT_DISPLAY_READY;
                    valid_cmd = true;
                } else if (strcmp(line_buf, "START_LOGIN") == 0) {
                    evt.type = EVT_CMD_START_LOGIN;
                    valid_cmd = true;
                } else if (strcmp(line_buf, "START_ENROLLMENT") == 0) {
                    evt.type = EVT_CMD_START_ENROLLMENT;
                    valid_cmd = true;
                } else if (strcmp(line_buf, "READ_OXIMETER") == 0) {
                    evt.type = EVT_CMD_READ_OXIMETER;
                    valid_cmd = true;
                } else if (strncmp(line_buf, "SAVE_READINGS", 13) == 0) {
                    evt.type = EVT_CMD_SAVE_READINGS;
                    valid_cmd = true;
                } else if (strstr(line_buf, "BACK")) {
                    evt.type = EVT_CMD_BACK;
                    valid_cmd = true;
                } else if (strcmp(line_buf, "LOGOUT") == 0) {
                    evt.type = EVT_CMD_LOGOUT;
                    valid_cmd = true;
                }

                if (valid_cmd && g_sys_event_queue) {
                    xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(10));
                }

                line_len = 0; // Reset for next line
            }
        } else if (line_len < sizeof(line_buf) - 1) {
            line_buf[line_len++] = (char)rx_byte;
        } else {
            // Line buffer overflow protection
            line_len = 0;
        }
    }
}

static void display_tx_task(void *pvParameters) {
    display_msg_t msg;
    ESP_LOGI(TAG, "Display TX Task started");

    while (1) {
        if (xQueueReceive(g_display_tx_queue, &msg, portMAX_DELAY) == pdTRUE) {
            size_t len = strlen(msg.data);
            if (len > 0) {
                uart_write_bytes(DISPLAY_UART_NUM, msg.data, len);
                uart_wait_tx_done(DISPLAY_UART_NUM, pdMS_TO_TICKS(100));
            }
        }
    }
}

esp_err_t display_comm_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = DISPLAY_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(DISPLAY_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config UART params: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(DISPLAY_UART_NUM, PIN_DISPLAY_TX, PIN_DISPLAY_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_driver_install(DISPLAY_UART_NUM, DISPLAY_RX_BUF_SIZE, DISPLAY_TX_BUF_SIZE, 10, &s_uart_event_queue, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        return err;
    }

    if (!g_display_tx_queue) {
        g_display_tx_queue = xQueueCreate(16, sizeof(display_msg_t));
    }

    xTaskCreatePinnedToCore(display_rx_task, "disp_rx_task", 3072, NULL, PRIO_DISPLAY_COMM, NULL, CORE_REALTIME_APP);
    xTaskCreatePinnedToCore(display_tx_task, "disp_tx_task", 3072, NULL, PRIO_DISPLAY_COMM, NULL, CORE_REALTIME_APP);

    ESP_LOGI(TAG, "Display UART initialized on TX=%d, RX=%d at %d baud", PIN_DISPLAY_TX, PIN_DISPLAY_RX, DISPLAY_BAUDRATE);
    return ESP_OK;
}

void display_send_raw(const char *format, ...) {
    if (!g_display_tx_queue) return;

    display_msg_t msg;
    va_list args;
    va_start(args, format);
    vsnprintf(msg.data, sizeof(msg.data), format, args);
    va_end(args);

    xQueueSend(g_display_tx_queue, &msg, pdMS_TO_TICKS(20));
}

void display_send_prompt(const char *message) {
    display_send_raw("{\"type\":\"PROMPT\",\"message\":\"%s\"}\n", message ? message : "");
}

void display_send_status(const char *message, bool wifi_ok, const char *ip_str, bool firebase_ok) {
    display_send_raw(
        "{\"type\":\"SYSTEM_STATUS\",\"message\":\"%s\",\"wifi_connected\":%s,\"ip\":\"%s\",\"firebase_connected\":%s,\"ready\":%s}\n",
        message ? message : "",
        wifi_ok ? "true" : "false",
        ip_str ? ip_str : "0.0.0.0",
        firebase_ok ? "true" : "false",
        (wifi_ok && firebase_ok) ? "true" : "false"
    );
}

void display_send_user_data(const user_profile_t *user) {
    if (!user) return;
    display_send_raw(
        "{\"type\":\"USER_DATA\",\"user_name\":\"%s\",\"first_name\":\"%s\",\"last_name\":\"%s\","
        "\"email\":\"%s\",\"user_medical_id\":\"%s\",\"user_age\":\"%s\",\"user_gender\":\"%s\"}\n",
        user->name, user->first_name, user->last_name,
        user->email, user->medical_id, user->age, user->gender
    );
}

void display_send_sensor_data(const vital_readings_t *v) {
    if (!v) return;
    display_send_raw(
        "{\"type\":\"SENSOR_DATA\",\"heart_rate\":%.1f,\"spo2\":%.1f,\"temperature\":%.1f,"
        "\"weight\":%.1f,\"height\":%.2f,\"bmi\":%.1f}\n",
        v->heart_rate, v->spo2, v->temperature, v->weight, v->height_laser, v->bmi_laser
    );
}

void display_send_typed(const char *msg_type, const char *message) {
    display_send_raw("{\"type\":\"%s\",\"message\":\"%s\"}\n", msg_type ? msg_type : "", message ? message : "");
}
