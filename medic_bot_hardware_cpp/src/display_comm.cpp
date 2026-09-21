#include "display_comm.hpp"
#include "app_config.h"
#include "system_events.h"
#include "cJSON.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>
#include <cstdarg>

static const char *TAG = "DISP_COMM_CPP";

typedef struct {
    char data[256];
} display_tx_msg_t;

namespace medicbot {

static DisplayComm s_display_instance(DISPLAY_UART_NUM);

DisplayComm &getDisplay() {
    return s_display_instance;
}

DisplayComm::DisplayComm(uart_port_t uart_num)
    : m_uart_num(uart_num), m_tx_queue(nullptr), m_initialized(false) {}

DisplayComm::~DisplayComm() {
    if (m_initialized) {
        uart_driver_delete(m_uart_num);
        if (m_tx_queue) {
            vQueueDelete(m_tx_queue);
            m_tx_queue = nullptr;
        }
        m_initialized = false;
    }
}

void DisplayComm::rxTaskTrampoline(void *arg) {
    static_cast<DisplayComm *>(arg)->rxTask();
}

void DisplayComm::txTaskTrampoline(void *arg) {
    static_cast<DisplayComm *>(arg)->txTask();
}

esp_err_t DisplayComm::begin(int tx_pin, int rx_pin, uint32_t baud_rate) {
    const uart_config_t uart_config = {
        .baud_rate = (int)baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(m_uart_num, &uart_config);
    if (err != ESP_OK) return err;

    err = uart_set_pin(m_uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;

    err = uart_driver_install(m_uart_num, 1024, 1024, 0, nullptr, 0);
    if (err != ESP_OK) return err;

    m_tx_queue = xQueueCreate(16, sizeof(display_tx_msg_t));
    if (!m_tx_queue) return ESP_ERR_NO_MEM;

    m_initialized = true;

    xTaskCreatePinnedToCore(rxTaskTrampoline, "disp_rx", 3072, this, PRIO_DISPLAY_COMM, nullptr, CORE_SYS_NETWORK);
    xTaskCreatePinnedToCore(txTaskTrampoline, "disp_tx", 3072, this, PRIO_DISPLAY_COMM, nullptr, CORE_SYS_NETWORK);

    ESP_LOGI(TAG, "Display UART C++ driver initialized on TX=%d, RX=%d", tx_pin, rx_pin);
    return ESP_OK;
}

void DisplayComm::rxTask() {
    uint8_t rx_byte = 0;
    char line_buf[256];
    uint16_t line_len = 0;

    ESP_LOGI(TAG, "Display RX Task started on Core %d", xPortGetCoreID());

    while (1) {
        int len = uart_read_bytes(m_uart_num, &rx_byte, 1, pdMS_TO_TICKS(50));
        if (len <= 0) continue;

        if (rx_byte == '\n' || rx_byte == '\r') {
            if (line_len > 0) {
                line_buf[line_len] = '\0';
                ESP_LOGI(TAG, "Display RX: [%s]", line_buf);

                sys_event_t evt;
                memset(&evt, 0, sizeof(evt));
                bool valid = false;

                if (line_buf[0] == '{') {
                    cJSON *root = cJSON_Parse(line_buf);
                    if (root) {
                        cJSON *type = cJSON_GetObjectItem(root, "type");
                        cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
                        const char *t = cJSON_IsString(type) ? type->valuestring :
                                       (cJSON_IsString(cmd) ? cmd->valuestring : nullptr);

                        if (t) {
                            if (strcmp(t, "DISPLAY_READY") == 0) {
                                evt.type = EVT_DISPLAY_READY;
                                valid = true;
                            } else if (strcmp(t, "START_LOGIN") == 0) {
                                evt.type = EVT_CMD_START_LOGIN;
                                valid = true;
                            } else if (strcmp(t, "START_ENROLLMENT") == 0) {
                                evt.type = EVT_CMD_START_ENROLLMENT;
                                valid = true;
                            } else if (strcmp(t, "READ_OXIMETER") == 0 || strcmp(t, "START_VITALS_MEASUREMENT") == 0) {
                                evt.type = EVT_CMD_READ_OXIMETER;
                                valid = true;
                            } else if (strcmp(t, "MEASUREMENTS_DONE") == 0 || strcmp(t, "SAVE_READINGS") == 0) {
                                evt.type = EVT_CMD_SAVE_READINGS;
                                cJSON *sys = cJSON_GetObjectItem(root, "systolic");
                                cJSON *dia = cJSON_GetObjectItem(root, "diastolic");
                                if (cJSON_IsNumber(sys)) evt.payload.vitals.systolic = sys->valueint;
                                if (cJSON_IsNumber(dia)) evt.payload.vitals.diastolic = dia->valueint;
                                valid = true;
                            } else if (strcmp(t, "BACK") == 0) {
                                evt.type = EVT_CMD_BACK;
                                valid = true;
                            } else if (strcmp(t, "LOGOUT") == 0) {
                                evt.type = EVT_CMD_LOGOUT;
                                valid = true;
                            }
                        }
                        cJSON_Delete(root);
                    }
                }

                // Fallback for plain-text commands
                if (!valid) {
                    if (strstr(line_buf, "DISPLAY_READY")) {
                        evt.type = EVT_DISPLAY_READY;
                        valid = true;
                    } else if (strcmp(line_buf, "START_LOGIN") == 0) {
                        evt.type = EVT_CMD_START_LOGIN;
                        valid = true;
                    } else if (strcmp(line_buf, "START_ENROLLMENT") == 0) {
                        evt.type = EVT_CMD_START_ENROLLMENT;
                        valid = true;
                    } else if (strcmp(line_buf, "READ_OXIMETER") == 0) {
                        evt.type = EVT_CMD_READ_OXIMETER;
                        valid = true;
                    } else if (strcmp(line_buf, "BACK") == 0) {
                        evt.type = EVT_CMD_BACK;
                        valid = true;
                    }
                }

                if (valid && g_sys_event_queue) {
                    xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(10));
                }
                line_len = 0;
            }
        } else if (line_len < sizeof(line_buf) - 1) {
            line_buf[line_len++] = (char)rx_byte;
        } else {
            line_len = 0;
        }
    }
}

void DisplayComm::txTask() {
    display_tx_msg_t msg;
    ESP_LOGI(TAG, "Display TX Task started on Core %d", xPortGetCoreID());

    while (1) {
        if (xQueueReceive(m_tx_queue, &msg, portMAX_DELAY) == pdTRUE) {
            size_t len = strlen(msg.data);
            if (len > 0) {
                uart_write_bytes(m_uart_num, msg.data, len);
                uart_wait_tx_done(m_uart_num, pdMS_TO_TICKS(100));
            }
        }
    }
}

void DisplayComm::sendRaw(const char *format, ...) {
    if (!m_tx_queue) return;
    display_tx_msg_t msg;
    va_list args;
    va_start(args, format);
    vsnprintf(msg.data, sizeof(msg.data), format, args);
    va_end(args);
    xQueueSend(m_tx_queue, &msg, pdMS_TO_TICKS(20));
}

void DisplayComm::sendPrompt(const char *message) {
    sendRaw("{\"type\":\"PROMPT\",\"message\":\"%s\"}\n", message ? message : "");
}

void DisplayComm::sendBootProgress(int percent, const char *task, int core_ok, int wifi_ok, int cloud_ok, int sensors_ok) {
    sendRaw(
        "{\"type\":\"BOOT_PROGRESS\",\"percent\":%d,\"task\":\"%s\",\"core\":%d,\"wifi\":%d,\"cloud\":%d,\"sensors\":%d}\n",
        percent, task ? task : "", core_ok, wifi_ok, cloud_ok, sensors_ok
    );
}

void DisplayComm::sendStatus(const char *message, bool wifi_ok, const char *ip_str, bool cloud_ok) {
    sendRaw(
        "{\"type\":\"SYSTEM_STATUS\",\"message\":\"%s\",\"wifi_connected\":%s,\"ip\":\"%s\",\"firebase_connected\":%s,\"ready\":%s}\n",
        message ? message : "",
        wifi_ok ? "true" : "false",
        ip_str ? ip_str : "0.0.0.0",
        cloud_ok ? "true" : "false",
        (wifi_ok && cloud_ok) ? "true" : "false"
    );
}

void DisplayComm::sendUserData(const user_profile_t *user) {
    if (!user) return;
    sendRaw(
        "{\"type\":\"USER_DATA\",\"user_name\":\"%s\",\"first_name\":\"%s\",\"last_name\":\"%s\","
        "\"email\":\"%s\",\"user_medical_id\":\"%s\",\"user_age\":\"%s\",\"user_gender\":\"%s\"}\n",
        user->name, user->first_name, user->last_name,
        user->email, user->medical_id, user->age, user->gender
    );
}

void DisplayComm::sendSensorData(const vital_readings_t *v) {
    if (!v) return;
    sendRaw(
        "{\"type\":\"SENSOR_DATA\",\"heart_rate\":%.1f,\"spo2\":%.1f,\"temperature\":%.1f,"
        "\"weight\":%.1f,\"height\":%.2f,\"bmi\":%.1f}\n",
        v->heart_rate, v->spo2, v->temperature, v->weight, v->height_laser, v->bmi_laser
    );
}

void DisplayComm::sendTyped(const char *msg_type, const char *message) {
    sendRaw("{\"type\":\"%s\",\"message\":\"%s\"}\n", msg_type ? msg_type : "", message ? message : "");
}

} // namespace medicbot
