#include "cloud_sync.hpp"
#include "app_config.h"
#include "system_events.h"
#include "wifi_manager.hpp"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <cstring>
#include <cstdio>
#include <ctime>
#include <strings.h>
#include <cctype>

static const char *TAG = "CLOUD_SYNC_CPP";

namespace medicbot {

static CloudSync s_cloud_instance;

CloudSync &getCloudSync() {
    return s_cloud_instance;
}

static char s_firebase_host[128] = FIREBASE_HOST_DEFAULT;
static char s_firebase_auth[128] = FIREBASE_API_KEY_DEF;

static void build_firebase_url(char *dest, size_t max_len, const char *path) {
    if (strlen(s_firebase_auth) > 0) {
        char sep = strchr(path, '?') ? '&' : '?';
        snprintf(dest, max_len, "%s%s%cauth=%s", s_firebase_host, path, sep, s_firebase_auth);
    } else {
        snprintf(dest, max_len, "%s%s", s_firebase_host, path);
    }
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    return ESP_OK;
}

static bool sync_reading_to_firebase(const vital_readings_t *r) {
    if (!getWifi().isConnected() || strlen(s_firebase_host) == 0) {
        return false;
    }

    char url[256];
    build_firebase_url(url, sizeof(url), "/READINGS/latest.json");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "heart_rate", r->heart_rate);
    cJSON_AddNumberToObject(root, "spo2", r->spo2);
    cJSON_AddNumberToObject(root, "temperature", r->temperature);
    cJSON_AddNumberToObject(root, "weight", r->weight);
    cJSON_AddNumberToObject(root, "height", r->height_laser);
    cJSON_AddNumberToObject(root, "bmi", r->bmi_laser);
    cJSON_AddNumberToObject(root, "systolic", r->systolic);
    cJSON_AddNumberToObject(root, "diastolic", r->diastolic);
    cJSON_AddNumberToObject(root, "timestamp", static_cast<double>(time(nullptr)));

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) return false;

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .timeout_ms = 5000,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_str, strlen(json_str));

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(json_str);

    if (err == ESP_OK && (status_code >= 200 && status_code < 300)) {
        ESP_LOGI(TAG, "Successfully synced reading to Firebase");
        return true;
    } else {
        ESP_LOGW(TAG, "Firebase sync failed, status: %d, err: %s", status_code, esp_err_to_name(err));
        return false;
    }
}

static bool fetch_user_from_firebase(const char *rfid, user_profile_t *out_user) {
    if (!getWifi().isConnected() || strlen(s_firebase_host) == 0 || !rfid || !out_user) {
        return false;
    }

    char path[128];
    snprintf(path, sizeof(path), "/USERS/%s.json", rfid);
    char url[256];
    build_firebase_url(url, sizeof(url), path);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return false;
    }

    esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    if (status_code != 200) {
        esp_http_client_cleanup(client);
        return false;
    }

    char response_buf[512] = {0};
    int read_len = esp_http_client_read(client, response_buf, sizeof(response_buf) - 1);
    esp_http_client_cleanup(client);

    if (read_len <= 0 || strcmp(response_buf, "null") == 0) {
        return false;
    }

    cJSON *json = cJSON_Parse(response_buf);
    if (!json) return false;

    memset(out_user, 0, sizeof(user_profile_t));
    strncpy(out_user->rfid_uid, rfid, sizeof(out_user->rfid_uid) - 1);

    cJSON *item = nullptr;
    item = cJSON_GetObjectItem(json, "name"); if (item && item->valuestring) strncpy(out_user->name, item->valuestring, sizeof(out_user->name) - 1);
    item = cJSON_GetObjectItem(json, "first_name"); if (item && item->valuestring) strncpy(out_user->first_name, item->valuestring, sizeof(out_user->first_name) - 1);
    item = cJSON_GetObjectItem(json, "last_name"); if (item && item->valuestring) strncpy(out_user->last_name, item->valuestring, sizeof(out_user->last_name) - 1);
    item = cJSON_GetObjectItem(json, "email"); if (item && item->valuestring) strncpy(out_user->email, item->valuestring, sizeof(out_user->email) - 1);
    item = cJSON_GetObjectItem(json, "age"); if (item && item->valuestring) strncpy(out_user->age, item->valuestring, sizeof(out_user->age) - 1);
    item = cJSON_GetObjectItem(json, "gender"); if (item && item->valuestring) strncpy(out_user->gender, item->valuestring, sizeof(out_user->gender) - 1);
    item = cJSON_GetObjectItem(json, "medical_id"); if (item && item->valuestring) strncpy(out_user->medical_id, item->valuestring, sizeof(out_user->medical_id) - 1);

    item = cJSON_GetObjectItem(json, "fingerprint_registered");
    out_user->biometric_enrolled = (item && cJSON_IsTrue(item));

    item = cJSON_GetObjectItem(json, "fingerprint_id");
    out_user->fingerprint_slot = item ? item->valueint : 0;

    cJSON_Delete(json);
    return true;
}

CloudSync::CloudSync()
    : m_ready(false) {}

CloudSync::~CloudSync() {}

void CloudSync::workerTaskTrampoline(void *arg) {
    static_cast<CloudSync *>(arg)->workerTask();
}

void CloudSync::workerTask() {
    ESP_LOGI(TAG, "Cloud sync worker task started on Core %d", xPortGetCoreID());
    cloud_cmd_t cmd;

    while (1) {
        if (xQueueReceive(g_cloud_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.type) {
                case CLOUD_CMD_SYNC_READING:
                    sync_reading_to_firebase(&cmd.data.reading);
                    break;

                case CLOUD_CMD_FETCH_USER: {
                    user_profile_t profile;
                    bool found = fetch_user_from_firebase(cmd.data.rfid, &profile);
                    if (g_sys_event_queue) {
                        sys_event_t evt;
                        memset(&evt, 0, sizeof(evt));
                        if (found) {
                            evt.type = EVT_USER_LOADED;
                            evt.payload.user = profile;
                        } else {
                            evt.type = EVT_USER_NOT_FOUND;
                            strncpy(evt.payload.rfid, cmd.data.rfid, sizeof(evt.payload.rfid) - 1);
                        }
                        xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(100));
                    }
                    break;
                }

                case CLOUD_CMD_UPDATE_FP_STATUS: {
                    if (getWifi().isConnected() && strlen(s_firebase_host) > 0) {
                        char path[128];
                        snprintf(path, sizeof(path), "/USERS/%s.json", cmd.data.fp_status.rfid);
                        char url[256];
                        build_firebase_url(url, sizeof(url), path);

                        cJSON *root = cJSON_CreateObject();
                        cJSON_AddBoolToObject(root, "fingerprint_registered", cmd.data.fp_status.registered);
                        cJSON_AddNumberToObject(root, "fingerprint_id", cmd.data.fp_status.fp_id);

                        char *json_str = cJSON_PrintUnformatted(root);
                        cJSON_Delete(root);

                        if (json_str) {
                            esp_http_client_config_t cfg = {
                                .url = url,
                                .method = HTTP_METHOD_PATCH,
                                .timeout_ms = 5000,
                                .crt_bundle_attach = esp_crt_bundle_attach,
                            };
                            esp_http_client_handle_t cl = esp_http_client_init(&cfg);
                            esp_http_client_set_header(cl, "Content-Type", "application/json");
                            esp_http_client_set_post_field(cl, json_str, strlen(json_str));
                            esp_http_client_perform(cl);
                            esp_http_client_cleanup(cl);
                            free(json_str);
                        }
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }
}

esp_err_t CloudSync::begin() {
    if (!g_cloud_queue) {
        g_cloud_queue = xQueueCreate(QUEUE_CLOUD_DEPTH, sizeof(cloud_cmd_t));
    }

    // 1. Try reading Firebase credentials from NVS Flash
    nvs_handle_t nvs_h;
    if (nvs_open("kiosk_cfg", NVS_READONLY, &nvs_h) == ESP_OK) {
        size_t h_len = sizeof(s_firebase_host);
        size_t a_len = sizeof(s_firebase_auth);
        nvs_get_str(nvs_h, "fb_host", s_firebase_host, &h_len);
        nvs_get_str(nvs_h, "fb_auth", s_firebase_auth, &a_len);
        nvs_close(nvs_h);
    }

    // 2. Try reading from SD Card candidate files if not in NVS
    if (strlen(s_firebase_host) == 0) {
        const char *candidate_paths[] = {
            "/sdcard/firebase.txt",
            "/sdcard/FIREBASE.TXT",
            "/sdcard/wifi.txt",
            "/sdcard/WIFI.TXT",
            "/sdcard/config.txt"
        };
        for (const char *path : candidate_paths) {
            FILE *f = fopen(path, "r");
            if (!f) continue;
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                line[strcspn(line, "\r\n")] = 0;
                char *eq = strchr(line, '=');
                if (eq) {
                    *eq = '\0';
                    char *key = line;
                    char *val = eq + 1;
                    while (*key == ' ' || *key == '\t') key++;
                    while (*val == ' ' || *val == '\t' || *val == '\"' || *val == '\'') val++;
                    size_t len = strlen(val);
                    while (len > 0 && (val[len-1] == ' ' || val[len-1] == '\t' || val[len-1] == '\"' || val[len-1] == '\'' || val[len-1] == '\r' || val[len-1] == '\n')) val[--len] = '\0';

                    if (strcasecmp(key, "FIREBASE_HOST") == 0 || strcasecmp(key, "FIREBASE_URL") == 0 || strcasecmp(key, "RTDB_URL") == 0 || strcasecmp(key, "DATABASE_URL") == 0 || strcasecmp(key, "FIREBASE_DATABASE_URL") == 0 || strcasecmp(key, "DB_URL") == 0 || strcasecmp(key, "FIREBASE") == 0) {
                        strncpy(s_firebase_host, val, sizeof(s_firebase_host) - 1);
                    } else if (strcasecmp(key, "FIREBASE_AUTH") == 0 || strcasecmp(key, "FIREBASE_SECRET") == 0 || strcasecmp(key, "FIREBASE_KEY") == 0 || strcasecmp(key, "AUTH") == 0 || strcasecmp(key, "SECRET") == 0 || strcasecmp(key, "API_KEY") == 0) {
                        strncpy(s_firebase_auth, val, sizeof(s_firebase_auth) - 1);
                    }
                }
            }
            fclose(f);
            if (strlen(s_firebase_host) > 0) break;
        }
    }

    // 3. Fallback to compile-time defaults if not provided in NVS or SD card
    if (strlen(s_firebase_host) == 0 && strlen(FIREBASE_HOST_DEFAULT) > 0) {
        strncpy(s_firebase_host, FIREBASE_HOST_DEFAULT, sizeof(s_firebase_host) - 1);
    }
    if (strlen(s_firebase_auth) == 0 && strlen(FIREBASE_API_KEY_DEF) > 0) {
        strncpy(s_firebase_auth, FIREBASE_API_KEY_DEF, sizeof(s_firebase_auth) - 1);
    }

    // Clean up trailing slash from host URL
    size_t host_len = strlen(s_firebase_host);
    while (host_len > 0 && s_firebase_host[host_len - 1] == '/') {
        s_firebase_host[--host_len] = '\0';
    }

    if (strlen(s_firebase_host) > 0) {
        ESP_LOGI(TAG, ">>> Firebase Realtime Database target: %s <<<", s_firebase_host);
    } else {
        ESP_LOGW(TAG, "Firebase Realtime Database URL not yet configured (sync dormant until set)");
    }

    m_ready = true;

    xTaskCreatePinnedToCore(
        workerTaskTrampoline,
        "cloud_task",
        STACK_CLOUD,
        this,
        PRIO_CLOUD_SYNC,
        nullptr,
        CORE_NETWORK_STORAGE
    );

    ESP_LOGI(TAG, "Cloud sync worker task started");
    return ESP_OK;
}

bool CloudSync::isConfigured() const {
    return strlen(s_firebase_host) > 0;
}

const char *CloudSync::getFirebaseHost() const {
    return s_firebase_host;
}

void CloudSync::testConnection() {
    if (!getWifi().isConnected() || strlen(s_firebase_host) == 0) {
        ESP_LOGW(TAG, "Cannot test Firebase connection: Wi-Fi disconnected or Firebase URL not configured");
        return;
    }

    char url[256];
    build_firebase_url(url, sizeof(url), "/.json?shallow=true");
    ESP_LOGI(TAG, "Testing Firebase Realtime Database connection: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 8000,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && (status_code >= 200 && status_code < 300)) {
        ESP_LOGI(TAG, ">>> SUCCESS: Firebase Realtime Database connection verified (HTTP %d)! <<<", status_code);
    } else {
        ESP_LOGW(TAG, "Firebase connection test returned status %d (err: %s)", status_code, esp_err_to_name(err));
    }
}

esp_err_t CloudSync::queueReading(const vital_readings_t *reading) {
    if (!g_cloud_queue || !reading) return ESP_ERR_INVALID_ARG;
    cloud_cmd_t cmd = {
        .type = CLOUD_CMD_SYNC_READING,
        .data = { .reading = *reading },
    };
    return (xQueueSend(g_cloud_queue, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

esp_err_t CloudSync::queueFetchUser(const char *rfid) {
    if (!g_cloud_queue || !rfid) return ESP_ERR_INVALID_ARG;
    cloud_cmd_t cmd;
    cmd.type = CLOUD_CMD_FETCH_USER;
    strncpy(cmd.data.rfid, rfid, sizeof(cmd.data.rfid) - 1);
    return (xQueueSend(g_cloud_queue, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

esp_err_t CloudSync::queueUpdateFp(const char *rfid, uint8_t fp_id) {
    if (!g_cloud_queue || !rfid) return ESP_ERR_INVALID_ARG;
    cloud_cmd_t cmd;
    cmd.type = CLOUD_CMD_UPDATE_FP_STATUS;
    strncpy(cmd.data.fp_status.rfid, rfid, sizeof(cmd.data.fp_status.rfid) - 1);
    cmd.data.fp_status.fp_id = fp_id;
    cmd.data.fp_status.registered = true;
    return (xQueueSend(g_cloud_queue, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

} // namespace medicbot

// C Bridge
extern "C" {

esp_err_t cloud_sync_init(void) {
    return medicbot::getCloudSync().begin();
}

esp_err_t cloud_sync_queue_reading(const vital_readings_t *reading) {
    return medicbot::getCloudSync().queueReading(reading);
}

esp_err_t cloud_sync_queue_fetch_user(const char *rfid) {
    return medicbot::getCloudSync().queueFetchUser(rfid);
}

esp_err_t cloud_sync_queue_update_fp(const char *rfid, uint8_t fp_id) {
    return medicbot::getCloudSync().queueUpdateFp(rfid, fp_id);
}

bool cloud_sync_is_ready(void) {
    return medicbot::getCloudSync().isReady();
}

}
