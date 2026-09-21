#include "wifi_manager.hpp"
#include "app_config.h"
#include "system_events.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include <cstring>
#include <cstdio>
#include <strings.h>
#include <cctype>

static const char *TAG = "WIFI_MGR_CPP";

static void trim_whitespace(char *str) {
    if (!str) return;
    char *p = str;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\"' || *p == '\'')) p++;
    if (p != str) memmove(str, p, strlen(p) + 1);
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' || str[len - 1] == '\r' || str[len - 1] == '\n' || str[len - 1] == '\"' || str[len - 1] == '\'')) {
        str[--len] = '\0';
    }
}

namespace medicbot {

static WifiManager s_wifi_instance;

WifiManager &getWifi() {
    return s_wifi_instance;
}

WifiManager::WifiManager()
    : m_connected(false), m_initialized(false) {
    memset(m_ip_addr, 0, sizeof(m_ip_addr));
    strncpy(m_ip_addr, "0.0.0.0", sizeof(m_ip_addr) - 1);
}

WifiManager::~WifiManager() {}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    WifiManager *mgr = static_cast<WifiManager *>(arg);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi station started, initiating connection...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        mgr->setConnected(false);
        ESP_LOGW(TAG, "Wi-Fi disconnected. Scheduling reconnection...");
        if (g_sys_event_queue) {
            sys_event_t evt = { .type = EVT_WIFI_DISCONNECTED };
            xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(50));
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = static_cast<ip_event_got_ip_t *>(event_data);
        char ip_str[16] = {0};
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        mgr->setConnected(true);

        ESP_LOGI(TAG, "Wi-Fi Connected! IP: %s", ip_str);

        // Initialize SNTP
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, NTP_SERVER);
        esp_sntp_init();

        if (g_sys_event_queue) {
            sys_event_t evt;
            memset(&evt, 0, sizeof(evt));
            evt.type = EVT_WIFI_CONNECTED;
            evt.payload.wifi_info.connected = true;
            strncpy(evt.payload.wifi_info.ip, ip_str, sizeof(evt.payload.wifi_info.ip) - 1);
            xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(50));
        }
    }
}

esp_err_t WifiManager::begin(const char *ssid, const char *password) {
    if (m_initialized) return ESP_OK;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        this,
                                                        nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        this,
                                                        nullptr));

    char loaded_ssid[33] = {0};
    char loaded_pass[65] = {0};
    bool found_config = false;

    // 1. Explicit arguments if provided
    if (ssid && strlen(ssid) > 0) {
        strncpy(loaded_ssid, ssid, sizeof(loaded_ssid) - 1);
        if (password) strncpy(loaded_pass, password, sizeof(loaded_pass) - 1);
        found_config = true;
    }

    // 2. Check SD Card: candidate files (/sdcard/wifi.txt, /sdcard/WIFI.TXT, /sdcard/wifi.cfg, /sdcard/config.txt)
    if (!found_config) {
        const char *candidate_paths[] = {
            "/sdcard/wifi.txt",
            "/sdcard/WIFI.TXT",
            "/sdcard/wifi.cfg",
            "/sdcard/config.txt",
            "/sdcard/CONFIG.TXT"
        };

        for (const char *path : candidate_paths) {
            FILE *f = fopen(path, "r");
            if (!f) continue;

            ESP_LOGI(TAG, "Parsing configuration from SD Card file: %s", path);
            char line[256];
            char raw_lines[5][128];
            int raw_count = 0;
            char loaded_fb_host[128] = {0};
            char loaded_fb_auth[128] = {0};

            while (fgets(line, sizeof(line), f)) {
                line[strcspn(line, "\r\n")] = 0;
                trim_whitespace(line);
                if (strlen(line) == 0 || line[0] == '#' || (line[0] == '/' && line[1] == '/')) {
                    continue;
                }

                if (raw_count < 5) {
                    strncpy(raw_lines[raw_count], line, sizeof(raw_lines[raw_count]) - 1);
                    raw_count++;
                }

                char *eq = strchr(line, '=');
                if (eq) {
                    *eq = '\0';
                    char *key = line;
                    char *val = eq + 1;
                    trim_whitespace(key);
                    trim_whitespace(val);

                    if (strcasecmp(key, "SSID") == 0 || strcasecmp(key, "WIFI_SSID") == 0) {
                        strncpy(loaded_ssid, val, sizeof(loaded_ssid) - 1);
                    } else if (strcasecmp(key, "PASSWORD") == 0 || strcasecmp(key, "PASS") == 0 || strcasecmp(key, "WIFI_PASS") == 0 || strcasecmp(key, "PWD") == 0) {
                        strncpy(loaded_pass, val, sizeof(loaded_pass) - 1);
                    } else if (strcasecmp(key, "FIREBASE_HOST") == 0 || strcasecmp(key, "FIREBASE_URL") == 0 || strcasecmp(key, "RTDB_URL") == 0 || strcasecmp(key, "DATABASE_URL") == 0 || strcasecmp(key, "FIREBASE_DATABASE_URL") == 0 || strcasecmp(key, "DB_URL") == 0 || strcasecmp(key, "FIREBASE") == 0) {
                        strncpy(loaded_fb_host, val, sizeof(loaded_fb_host) - 1);
                    } else if (strcasecmp(key, "FIREBASE_AUTH") == 0 || strcasecmp(key, "FIREBASE_SECRET") == 0 || strcasecmp(key, "FIREBASE_KEY") == 0 || strcasecmp(key, "AUTH") == 0 || strcasecmp(key, "SECRET") == 0 || strcasecmp(key, "API_KEY") == 0) {
                        strncpy(loaded_fb_auth, val, sizeof(loaded_fb_auth) - 1);
                    }
                }
            }
            fclose(f);

            // Fallback: If no KEY=VALUE syntax found, use line 1 as SSID and line 2 as Password
            if (strlen(loaded_ssid) == 0 && raw_count >= 1) {
                strncpy(loaded_ssid, raw_lines[0], sizeof(loaded_ssid) - 1);
                trim_whitespace(loaded_ssid);
                if (raw_count >= 2) {
                    strncpy(loaded_pass, raw_lines[1], sizeof(loaded_pass) - 1);
                    trim_whitespace(loaded_pass);
                }
                ESP_LOGI(TAG, "Extracted credentials from plain lines (SSID: %s)", loaded_ssid);
            }

            if (strlen(loaded_ssid) > 0) {
                ESP_LOGI(TAG, ">>> Loaded Wi-Fi credentials from %s (SSID: '%s') <<<", path, loaded_ssid);
                found_config = true;

                // Persist to NVS so settings are preserved
                nvs_handle_t nvs_h;
                if (nvs_open("kiosk_cfg", NVS_READWRITE, &nvs_h) == ESP_OK) {
                    nvs_set_str(nvs_h, "wifi_ssid", loaded_ssid);
                    nvs_set_str(nvs_h, "wifi_pass", loaded_pass);
                    if (strlen(loaded_fb_host) > 0) {
                        nvs_set_str(nvs_h, "fb_host", loaded_fb_host);
                        ESP_LOGI(TAG, "Saved Firebase Database URL to NVS: %s", loaded_fb_host);
                    }
                    if (strlen(loaded_fb_auth) > 0) {
                        nvs_set_str(nvs_h, "fb_auth", loaded_fb_auth);
                        ESP_LOGI(TAG, "Saved Firebase Auth Key to NVS");
                    }
                    nvs_commit(nvs_h);
                    nvs_close(nvs_h);
                }
                break; // Successfully loaded from this file
            }
        }
    }

    // 3. Check NVS Flash
    if (!found_config) {
        nvs_handle_t nvs_h;
        if (nvs_open("kiosk_cfg", NVS_READONLY, &nvs_h) == ESP_OK) {
            size_t s_len = sizeof(loaded_ssid);
            size_t p_len = sizeof(loaded_pass);
            if (nvs_get_str(nvs_h, "wifi_ssid", loaded_ssid, &s_len) == ESP_OK) {
                nvs_get_str(nvs_h, "wifi_pass", loaded_pass, &p_len);
                ESP_LOGI(TAG, "Loaded Wi-Fi credentials from NVS Flash (SSID: %s)", loaded_ssid);
                found_config = true;
            }
            nvs_close(nvs_h);
        }
    }

    // 4. Default Fallback
    if (!found_config) {
        strncpy(loaded_ssid, WIFI_SSID_DEFAULT, sizeof(loaded_ssid) - 1);
        strncpy(loaded_pass, WIFI_PASS_DEFAULT, sizeof(loaded_pass) - 1);
        ESP_LOGI(TAG, "Using default compile-time Wi-Fi credentials (SSID: %s)", loaded_ssid);
    }

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid), loaded_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy(reinterpret_cast<char *>(wifi_config.sta.password), loaded_pass, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    m_initialized = true;
    ESP_LOGI(TAG, "Wi-Fi Station Initialized targeting SSID: %s", loaded_ssid);
    return ESP_OK;
}

void WifiManager::getIp(char *buf, size_t max_len) {
    if (!buf || max_len == 0) return;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            esp_ip4addr_ntoa(&ip_info.ip, buf, max_len);
            return;
        }
    }
    strncpy(buf, "0.0.0.0", max_len - 1);
}

esp_err_t WifiManager::getChannel(uint8_t *channel) {
    if (!channel) return ESP_ERR_INVALID_ARG;
    wifi_second_chan_t second;
    return esp_wifi_get_channel(channel, &second);
}

} // namespace medicbot

// C Bridge
extern "C" {

esp_err_t wifi_manager_init(const char *ssid, const char *password) {
    return medicbot::getWifi().begin(ssid, password);
}

bool wifi_manager_is_connected(void) {
    return medicbot::getWifi().isConnected();
}

void wifi_manager_get_ip(char *buf, size_t max_len) {
    medicbot::getWifi().getIp(buf, max_len);
}

esp_err_t wifi_manager_get_channel(uint8_t *channel) {
    return medicbot::getWifi().getChannel(channel);
}

}
