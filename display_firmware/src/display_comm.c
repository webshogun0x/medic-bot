#include "display_comm.h"
#include "bsp.h"
#include "ui_screens.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static const char *TAG = "DISP_COMM";

patient_record_t g_active_patient = {
    .name = "Sarah Jenkins",
    .medical_id = "MB-94021",
    .age = "28 yrs",
    .gender = "Female",
    .weight = 68.5f,
    .height = 1.75f,
    .bmi = 22.4f,
    .heart_rate = 74.0f,
    .spo2 = 98.8f,
    .temperature = 36.7f,
    .systolic = 120,
    .diastolic = 80
};

typedef struct {
    char data[256];
} uart_tx_msg_t;

static QueueHandle_t s_tx_queue = NULL;

static void parse_json_line(const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        return;
    }

    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type_item)) {
        cJSON_Delete(root);
        return;
    }
    const char *type = type_item->valuestring;

    if (strcmp(type, "BOOT_PROGRESS") == 0) {
        cJSON *pct = cJSON_GetObjectItem(root, "percent");
        cJSON *task = cJSON_GetObjectItem(root, "task");
        cJSON *core = cJSON_GetObjectItem(root, "core");
        cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
        cJSON *cloud = cJSON_GetObjectItem(root, "cloud");
        cJSON *sensors = cJSON_GetObjectItem(root, "sensors");

        int p = cJSON_IsNumber(pct) ? pct->valueint : 0;
        const char *t = cJSON_IsString(task) ? task->valuestring : "";
        int c = cJSON_IsNumber(core) ? core->valueint : 0;
        int w = cJSON_IsNumber(wifi) ? wifi->valueint : 0;
        int cl = cJSON_IsNumber(cloud) ? cloud->valueint : 0;
        int s = cJSON_IsNumber(sensors) ? sensors->valueint : 0;

        ui_boot_update_status(p, t, c, w, cl, s);
    } else if (strcmp(type, "SYSTEM_STATUS") == 0) {
        cJSON *wifi = cJSON_GetObjectItem(root, "wifi_connected");
        cJSON *fb = cJSON_GetObjectItem(root, "firebase_connected");
        bool wifi_ok = cJSON_IsTrue(wifi);
        bool fb_ok = cJSON_IsTrue(fb);
        ui_boot_update_status(100, "All Subsystems Connected", 2, wifi_ok ? 2 : 1, fb_ok ? 2 : 1, 2);
    } else if (strcmp(type, "USER_DATA") == 0) {
        cJSON *name = cJSON_GetObjectItem(root, "user_name");
        cJSON *id = cJSON_GetObjectItem(root, "user_medical_id");
        cJSON *age = cJSON_GetObjectItem(root, "user_age");
        cJSON *gender = cJSON_GetObjectItem(root, "user_gender");

        if (cJSON_IsString(name)) strncpy(g_active_patient.name, name->valuestring, sizeof(g_active_patient.name) - 1);
        if (cJSON_IsString(id)) strncpy(g_active_patient.medical_id, id->valuestring, sizeof(g_active_patient.medical_id) - 1);
        if (cJSON_IsString(age)) strncpy(g_active_patient.age, age->valuestring, sizeof(g_active_patient.age) - 1);
        if (cJSON_IsString(gender)) strncpy(g_active_patient.gender, gender->valuestring, sizeof(g_active_patient.gender) - 1);

        ui_dashboard_update_vitals(&g_active_patient);
        ui_show_screen(UI_SCREEN_DASHBOARD);
    } else if (strcmp(type, "SENSOR_DATA") == 0) {
        cJSON *hr = cJSON_GetObjectItem(root, "heart_rate");
        cJSON *spo2 = cJSON_GetObjectItem(root, "spo2");
        cJSON *temp = cJSON_GetObjectItem(root, "temperature");
        cJSON *wt = cJSON_GetObjectItem(root, "weight");
        cJSON *ht = cJSON_GetObjectItem(root, "height");
        cJSON *bmi = cJSON_GetObjectItem(root, "bmi");

        if (cJSON_IsNumber(hr)) g_active_patient.heart_rate = (float)hr->valuedouble;
        if (cJSON_IsNumber(spo2)) g_active_patient.spo2 = (float)spo2->valuedouble;
        if (cJSON_IsNumber(temp)) g_active_patient.temperature = (float)temp->valuedouble;
        if (cJSON_IsNumber(wt)) g_active_patient.weight = (float)wt->valuedouble;
        if (cJSON_IsNumber(ht)) g_active_patient.height = (float)ht->valuedouble;
        if (cJSON_IsNumber(bmi)) g_active_patient.bmi = (float)bmi->valuedouble;

        ui_dashboard_update_vitals(&g_active_patient);
    } else if (strcmp(type, "PROMPT") == 0) {
        cJSON *msg = cJSON_GetObjectItem(root, "message");
        if (cJSON_IsString(msg)) {
            ui_show_toast(msg->valuestring);
        }
    } else if (strcmp(type, "CARD_DETECTED") == 0) {
        ui_show_screen(UI_SCREEN_LOGIN_FP);
        ui_show_toast("Card Verified! Place finger on scanner.");
    } else if (strcmp(type, "FINGERPRINT_SUCCESS") == 0) {
        ui_show_toast("Biometric Verified! Welcome.");
        ui_show_screen(UI_SCREEN_DASHBOARD);
    } else if (strcmp(type, "FINGERPRINT_ERROR") == 0) {
        cJSON *msg = cJSON_GetObjectItem(root, "message");
        ui_show_toast(cJSON_IsString(msg) ? msg->valuestring : "Fingerprint error. Try again.");
    } else if (strcmp(type, "ENROLL_STEP1") == 0) {
        ui_show_screen(UI_SCREEN_SIGNUP_FP1);
        ui_show_toast("Scan 1 of 2: Place finger on scanner");
    } else if (strcmp(type, "ENROLL_STEP2") == 0) {
        ui_show_screen(UI_SCREEN_SIGNUP_FP2);
        ui_show_toast("Scan 2 of 2: Place same finger again");
    } else if (strcmp(type, "ENROLL_DONE") == 0) {
        ui_show_toast("✅ Biometrics Enrolled! Loading Dashboard...");
        ui_show_screen(UI_SCREEN_DASHBOARD);
    } else if (strcmp(type, "ENROLL_FAILED") == 0) {
        cJSON *msg = cJSON_GetObjectItem(root, "message");
        ui_show_toast(cJSON_IsString(msg) ? msg->valuestring : "❌ Enrollment Failed.");
        ui_show_screen(UI_SCREEN_IDLE);
    }

    cJSON_Delete(root);
}

static void uart_rx_task(void *pvParameters) {
    uint8_t byte = 0;
    char line[256];
    size_t idx = 0;

    ESP_LOGI(TAG, "UART RX Task started on Core 0");

    while (1) {
        int len = uart_read_bytes(DISPLAY_UART_NUM, &byte, 1, pdMS_TO_TICKS(50));
        if (len > 0) {
            if (byte == '\n' || byte == '\r') {
                if (idx > 0) {
                    line[idx] = '\0';
                    ESP_LOGI(TAG, "RX: %s", line);
                    parse_json_line(line);
                    idx = 0;
                }
            } else if (idx < sizeof(line) - 1) {
                line[idx++] = (char)byte;
            } else {
                idx = 0;
            }
        }
    }
}

static void uart_tx_task(void *pvParameters) {
    uart_tx_msg_t msg;
    ESP_LOGI(TAG, "UART TX Task started on Core 0");

    while (1) {
        if (xQueueReceive(s_tx_queue, &msg, portMAX_DELAY) == pdTRUE) {
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
        .baud_rate = DISPLAY_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(DISPLAY_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(DISPLAY_UART_NUM, DISPLAY_UART_TX_PIN, DISPLAY_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(DISPLAY_UART_NUM, 1024, 1024, 0, NULL, 0));

    s_tx_queue = xQueueCreate(16, sizeof(uart_tx_msg_t));

    // Both UART tasks pinned to Core 0 to leave Core 1 for LVGL rendering
    xTaskCreatePinnedToCore(uart_rx_task, "uart_rx", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(uart_tx_task, "uart_tx", 3072, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "Display UART initialized on TX=%d, RX=%d", DISPLAY_UART_TX_PIN, DISPLAY_UART_RX_PIN);
    return ESP_OK;
}

void display_comm_send_cmd(const char *cmd) {
    if (!s_tx_queue || !cmd) return;
    uart_tx_msg_t msg;
    snprintf(msg.data, sizeof(msg.data), "%s\n", cmd);
    xQueueSend(s_tx_queue, &msg, pdMS_TO_TICKS(20));
}

void display_comm_send_raw(const char *format, ...) {
    if (!s_tx_queue || !format) return;
    uart_tx_msg_t msg;
    va_list args;
    va_start(args, format);
    vsnprintf(msg.data, sizeof(msg.data), format, args);
    va_end(args);
    xQueueSend(s_tx_queue, &msg, pdMS_TO_TICKS(20));
}

void display_comm_send_vitals(const patient_record_t *p) {
    if (!p) return;
    display_comm_send_raw(
        "{\"type\":\"MEASUREMENTS_DONE\",\"name\":\"%s\",\"id\":\"%s\","
        "\"weight\":%.1f,\"height\":%.2f,\"bmi\":%.1f,\"heart_rate\":%.1f,\"spo2\":%.1f,\"temperature\":%.1f,"
        "\"systolic\":%d,\"diastolic\":%d}\n",
        p->name, p->medical_id,
        p->weight, p->height, p->bmi, p->heart_rate, p->spo2, p->temperature,
        p->systolic, p->diastolic
    );
}
