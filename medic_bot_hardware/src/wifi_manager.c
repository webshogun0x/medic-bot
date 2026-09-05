#include "wifi_manager.h"
#include "app_config.h"
#include "system_events.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <time.h>

static const char *TAG = "WIFI_MGR";

static bool s_connected = false;
static char s_ip_str[16] = "0.0.0.0";
static SemaphoreHandle_t s_wifi_mutex = NULL;
static esp_netif_t *s_netif_sta = NULL;
static int s_retry_count = 0;

static void initialize_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    esp_sntp_init();

    // Set timezone
    setenv("TZ", NTP_TZ, 1);
    tzset();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started, connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xSemaphoreTake(s_wifi_mutex, portMAX_DELAY);
        s_connected = false;
        strncpy(s_ip_str, "0.0.0.0", sizeof(s_ip_str));
        xSemaphoreGive(s_wifi_mutex);

        ESP_LOGW(TAG, "WiFi disconnected, retrying... (attempt %d)", s_retry_count + 1);
        s_retry_count++;
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();

        if (g_sys_event_queue) {
            sys_event_t evt = { .type = EVT_WIFI_DISCONNECTED };
            xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(20));
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_retry_count = 0;

        xSemaphoreTake(s_wifi_mutex, portMAX_DELAY);
        s_connected = true;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        xSemaphoreGive(s_wifi_mutex);

        ESP_LOGI(TAG, "WiFi Connected! Got IP: %s", s_ip_str);
        initialize_sntp();

        if (g_sys_event_queue) {
            sys_event_t evt;
            memset(&evt, 0, sizeof(evt));
            evt.type = EVT_WIFI_CONNECTED;
            evt.payload.wifi_info.connected = true;
            strncpy(evt.payload.wifi_info.ip, s_ip_str, sizeof(evt.payload.wifi_info.ip) - 1);
            xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(50));
        }
    }
}

esp_err_t wifi_manager_init(const char *ssid, const char *password) {
    if (!s_wifi_mutex) {
        s_wifi_mutex = xSemaphoreCreateMutex();
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return ret;

    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop if not already created
    esp_err_t loop_err = esp_event_loop_create_default();
    if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) {
        return loop_err;
    }

    s_netif_sta = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };

    const char *target_ssid = (ssid && strlen(ssid) > 0) ? ssid : WIFI_SSID_DEFAULT;
    const char *target_pass = (password && strlen(password) > 0) ? password : WIFI_PASS_DEFAULT;

    strncpy((char *)wifi_config.sta.ssid, target_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, target_pass, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi Manager initialized in Station mode for SSID [%s]", target_ssid);
    return ESP_OK;
}

bool wifi_manager_is_connected(void) {
    bool conn = false;
    if (s_wifi_mutex) {
        xSemaphoreTake(s_wifi_mutex, portMAX_DELAY);
        conn = s_connected;
        xSemaphoreGive(s_wifi_mutex);
    }
    return conn;
}

void wifi_manager_get_ip(char *buf, size_t max_len) {
    if (!buf || max_len == 0) return;
    if (s_wifi_mutex) {
        xSemaphoreTake(s_wifi_mutex, portMAX_DELAY);
        strncpy(buf, s_ip_str, max_len - 1);
        buf[max_len - 1] = '\0';
        xSemaphoreGive(s_wifi_mutex);
    }
}

esp_err_t wifi_manager_get_channel(uint8_t *channel) {
    if (!channel) return ESP_ERR_INVALID_ARG;
    wifi_second_chan_t second;
    return esp_wifi_get_channel(channel, &second);
}
