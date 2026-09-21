#include "storage_manager.hpp"
#include "app_config.h"
#include "system_events.h"
#include "sqlite3.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include <cstring>
#include <cstdio>
#include <ctime>
#include <dirent.h>

static const char *TAG = "STORAGE_MGR_CPP";

namespace medicbot {

static StorageManager s_storage_instance;

StorageManager &getStorage() {
    return s_storage_instance;
}

StorageManager::StorageManager()
    : m_db_ready(false), m_db(nullptr), m_db_mutex(nullptr) {}

StorageManager::~StorageManager() {
    if (m_db) {
        sqlite3_close(static_cast<sqlite3 *>(m_db));
        m_db = nullptr;
    }
    if (m_db_mutex) {
        vSemaphoreDelete(m_db_mutex);
        m_db_mutex = nullptr;
    }
}

bool StorageManager::executeSql(const char *sql) {
    if (!m_db) return false;
    char *err_msg = nullptr;
    int rc = sqlite3_exec(static_cast<sqlite3 *>(m_db), sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "SQL exec error: %s", err_msg ? err_msg : "unknown");
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

void StorageManager::initTables() {
    const char *users_table_sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "rfid TEXT PRIMARY KEY,"
        "name TEXT NOT NULL,"
        "first_name TEXT,"
        "last_name TEXT,"
        "email TEXT,"
        "age TEXT,"
        "gender TEXT,"
        "medical_id TEXT,"
        "fingerprint_registered INTEGER DEFAULT 0,"
        "fingerprint_id INTEGER DEFAULT 0,"
        "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
        "last_login TEXT,"
        "login_count INTEGER DEFAULT 0"
        ");";
    executeSql(users_table_sql);

    const char *readings_table_sql =
        "CREATE TABLE IF NOT EXISTS health_readings ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "rfid TEXT NOT NULL,"
        "timestamp TEXT NOT NULL,"
        "heart_rate REAL,"
        "spo2 REAL,"
        "temperature REAL,"
        "weight REAL,"
        "height REAL,"
        "bmi_sonar REAL,"
        "bmi_laser REAL,"
        "systolic INTEGER,"
        "diastolic INTEGER,"
        "synced_to_firebase INTEGER DEFAULT 0,"
        "FOREIGN KEY (rfid) REFERENCES users(rfid)"
        ");";
    executeSql(readings_table_sql);

    executeSql("CREATE INDEX IF NOT EXISTS idx_readings_rfid ON health_readings(rfid);");
    executeSql("CREATE INDEX IF NOT EXISTS idx_readings_timestamp ON health_readings(timestamp);");
    executeSql("CREATE INDEX IF NOT EXISTS idx_readings_synced ON health_readings(synced_to_firebase);");
}

bool StorageManager::getUser(const char *rfid, user_profile_t &out_user) {
    if (!m_db_ready || !rfid) return false;

    xSemaphoreTake(m_db_mutex, portMAX_DELAY);
    const char *sql = "SELECT rfid, name, first_name, last_name, email, age, gender, medical_id, "
                      "fingerprint_registered, fingerprint_id FROM users WHERE rfid = ?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(static_cast<sqlite3 *>(m_db), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        xSemaphoreGive(m_db_mutex);
        return false;
    }

    sqlite3_bind_text(stmt, 1, rfid, -1, SQLITE_STATIC);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        memset(&out_user, 0, sizeof(user_profile_t));
        const char *t;
        t = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        if (t) {
            strncpy(out_user.rfid_uid, t, sizeof(out_user.rfid_uid) - 1);
        }
        t = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        if (t) strncpy(out_user.name, t, sizeof(out_user.name) - 1);
        t = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        if (t) strncpy(out_user.first_name, t, sizeof(out_user.first_name) - 1);
        t = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        if (t) strncpy(out_user.last_name, t, sizeof(out_user.last_name) - 1);
        t = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        if (t) strncpy(out_user.email, t, sizeof(out_user.email) - 1);
        t = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
        if (t) strncpy(out_user.age, t, sizeof(out_user.age) - 1);
        t = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
        if (t) strncpy(out_user.gender, t, sizeof(out_user.gender) - 1);
        t = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
        if (t) strncpy(out_user.medical_id, t, sizeof(out_user.medical_id) - 1);
        out_user.biometric_enrolled = (sqlite3_column_int(stmt, 8) != 0);
        out_user.fingerprint_slot = sqlite3_column_int(stmt, 9);
        found = true;
    }

    sqlite3_finalize(stmt);

    if (found) {
        const char *update_sql = "UPDATE users SET last_login = CURRENT_TIMESTAMP, login_count = login_count + 1 WHERE rfid = ?;";
        if (sqlite3_prepare_v2(static_cast<sqlite3 *>(m_db), update_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, rfid, -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    xSemaphoreGive(m_db_mutex);
    return found;
}

bool StorageManager::saveUser(const user_profile_t &user) {
    if (!m_db_ready) return false;

    xSemaphoreTake(m_db_mutex, portMAX_DELAY);
    const char *sql =
        "INSERT OR REPLACE INTO users "
        "(rfid, name, first_name, last_name, email, age, gender, medical_id, fingerprint_registered, fingerprint_id, login_count) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, COALESCE((SELECT login_count FROM users WHERE rfid = ?), 0));";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(static_cast<sqlite3 *>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        xSemaphoreGive(m_db_mutex);
        return false;
    }

    sqlite3_bind_text(stmt, 1, user.rfid_uid, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user.name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, user.first_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, user.last_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, user.email, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, user.age, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, user.gender, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, user.medical_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 9, user.biometric_enrolled ? 1 : 0);
    sqlite3_bind_int(stmt, 10, user.fingerprint_slot);
    sqlite3_bind_text(stmt, 11, user.rfid_uid, -1, SQLITE_STATIC);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    xSemaphoreGive(m_db_mutex);

    if (ok) {
        ESP_LOGI(TAG, "Saved user profile [%s] to SQLite", user.rfid_uid);
    }
    return ok;
}

bool StorageManager::saveReading(const vital_readings_t &r) {
    if (!m_db_ready) return false;

    xSemaphoreTake(m_db_mutex, portMAX_DELAY);
    const char *sql =
        "INSERT INTO health_readings "
        "(rfid, timestamp, heart_rate, spo2, temperature, weight, height, bmi_sonar, bmi_laser, systolic, diastolic, synced_to_firebase) "
        "VALUES (?, datetime('now'), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(static_cast<sqlite3 *>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        xSemaphoreGive(m_db_mutex);
        return false;
    }

    // Default to latest RFID if none attached directly to reading struct
    sqlite3_bind_text(stmt, 1, "PATIENT_RECORD", -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 2, r.heart_rate);
    sqlite3_bind_double(stmt, 3, r.spo2);
    sqlite3_bind_double(stmt, 4, r.temperature);
    sqlite3_bind_double(stmt, 5, r.weight);
    sqlite3_bind_double(stmt, 6, r.height_laser);
    sqlite3_bind_double(stmt, 7, r.bmi_sonar);
    sqlite3_bind_double(stmt, 8, r.bmi_laser);
    sqlite3_bind_int(stmt, 9, r.systolic);
    sqlite3_bind_int(stmt, 10, r.diastolic);
    sqlite3_bind_int(stmt, 11, 0);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    xSemaphoreGive(m_db_mutex);

    if (ok) {
        ESP_LOGI(TAG, "Saved health reading to SQLite (HR=%.1f, SpO2=%.1f, BP=%d/%d)",
                 r.heart_rate, r.spo2, r.systolic, r.diastolic);
    }
    return ok;
}

bool StorageManager::updateFingerprint(const char *rfid, uint8_t fp_id) {
    if (!m_db_ready || !rfid) return false;

    xSemaphoreTake(m_db_mutex, portMAX_DELAY);
    const char *sql = "UPDATE users SET fingerprint_registered = 1, fingerprint_id = ? WHERE rfid = ?;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(static_cast<sqlite3 *>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        xSemaphoreGive(m_db_mutex);
        return false;
    }

    sqlite3_bind_int(stmt, 1, fp_id);
    sqlite3_bind_text(stmt, 2, rfid, -1, SQLITE_STATIC);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    xSemaphoreGive(m_db_mutex);
    return ok;
}

void StorageManager::workerTaskTrampoline(void *arg) {
    static_cast<StorageManager *>(arg)->workerTask();
}

void StorageManager::workerTask() {
    ESP_LOGI(TAG, "Storage worker task started on Core %d", xPortGetCoreID());
    storage_cmd_t cmd;

    while (1) {
        if (xQueueReceive(g_storage_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.type) {
                case STORE_CMD_LOAD_USER: {
                    user_profile_t profile;
                    bool found = getUser(cmd.data.rfid, profile);
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
                case STORE_CMD_SAVE_USER:
                    saveUser(cmd.data.user);
                    break;

                case STORE_CMD_SAVE_READING: {
                    bool saved = saveReading(cmd.data.reading);
                    if (saved && g_sys_event_queue) {
                        sys_event_t evt;
                        memset(&evt, 0, sizeof(evt));
                        evt.type = EVT_READINGS_SAVED;
                        xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(50));
                    }
                    break;
                }
                case STORE_CMD_UPDATE_FP:
                    updateFingerprint(cmd.data.fp_update.rfid, cmd.data.fp_update.fp_id);
                    break;

                default:
                    break;
            }
        }
    }
}

esp_err_t StorageManager::begin() {
    if (!m_db_mutex) {
        m_db_mutex = xSemaphoreCreateMutex();
    }

    if (!g_storage_queue) {
        g_storage_queue = xQueueCreate(QUEUE_STORAGE_DEPTH, sizeof(storage_cmd_t));
    }

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI(TAG, "Mounting SD Card on SPI3 (CS:%d, MOSI:%d, SCK:%d, MISO:%d)...",
             PIN_SD_CS, PIN_SD_MOSI, PIN_SD_SCK, PIN_SD_MISO);

    gpio_set_pull_mode(PIN_SD_MISO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_SD_CS, GPIO_PULLUP_ONLY);

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = PIN_SD_MOSI;
    bus_cfg.miso_io_num = PIN_SD_MISO;
    bus_cfg.sclk_io_num = PIN_SD_SCK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000;

    esp_err_t ret = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    sdmmc_card_t *card = nullptr;

    if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = SD_SPI_HOST;
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;

        sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
        dev_cfg.gpio_cs = PIN_SD_CS;
        dev_cfg.host_id = SD_SPI_HOST;

        for (int attempt = 1; attempt <= 3; attempt++) {
            ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &dev_cfg, &mount_config, &card);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, ">>> SUCCESS: SD Card mounted on attempt %d! <<<", attempt);
                break;
            }
            ESP_LOGW(TAG, "SD mount attempt %d failed: %s, retrying...", attempt, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    sqlite3 *db_handle = nullptr;
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD Card mount failed: %s (Continuing in-memory SQLite fallback mode)", esp_err_to_name(ret));
        spi_bus_free(SD_SPI_HOST);
        sqlite3_initialize();
        if (sqlite3_open(":memory:", &db_handle) == SQLITE_OK) {
            m_db = db_handle;
            initTables();
            m_db_ready = true;
            ESP_LOGI(TAG, "In-memory SQLite database fallback initialized successfully");
        }
    } else {
        ESP_LOGI(TAG, "SD Card mounted at %s (%llu MB)", SD_MOUNT_POINT,
                 ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024));

        sqlite3_initialize();
        if (sqlite3_open(SD_DB_PATH, &db_handle) == SQLITE_OK) {
            m_db = db_handle;
            initTables();
            m_db_ready = true;
            ESP_LOGI(TAG, "SQLite database ready on SD Card (%s)", SD_DB_PATH);
        } else {
            ESP_LOGE(TAG, "Failed to open SQLite database file on SD Card, using in-memory");
            sqlite3_open(":memory:", &db_handle);
            m_db = db_handle;
            initTables();
            m_db_ready = true;
        }

        // Enumerate and log all files present in SD Card root directory
        DIR *sd_dir = opendir(SD_MOUNT_POINT);
        if (sd_dir) {
            ESP_LOGI(TAG, "Listing files on SD Card (%s):", SD_MOUNT_POINT);
            struct dirent *ent;
            int file_count = 0;
            while ((ent = readdir(sd_dir)) != nullptr) {
                ESP_LOGI(TAG, "  [FILE] %s", ent->d_name);
                file_count++;
            }
            closedir(sd_dir);
            if (file_count == 0) {
                ESP_LOGW(TAG, "  (No files found on SD Card root)");
            }
        } else {
            ESP_LOGW(TAG, "Could not open SD Card directory (%s) to list files", SD_MOUNT_POINT);
        }
    }

    xTaskCreatePinnedToCore(
        workerTaskTrampoline,
        "storage_task",
        STACK_STORAGE,
        this,
        PRIO_STORAGE,
        nullptr,
        CORE_NETWORK_STORAGE
    );

    return ESP_OK;
}

} // namespace medicbot

// C Bridge
extern "C" {

esp_err_t storage_manager_init(void) {
    return medicbot::getStorage().begin();
}

bool storage_manager_get_user(const char *rfid, user_profile_t *out_user) {
    if (!out_user) return false;
    return medicbot::getStorage().getUser(rfid, *out_user);
}

bool storage_manager_save_user(const user_profile_t *user) {
    if (!user) return false;
    return medicbot::getStorage().saveUser(*user);
}

bool storage_manager_save_reading(const vital_readings_t *reading) {
    if (!reading) return false;
    return medicbot::getStorage().saveReading(*reading);
}

bool storage_manager_update_fingerprint(const char *rfid, uint8_t fp_id) {
    return medicbot::getStorage().updateFingerprint(rfid, fp_id);
}

bool storage_manager_is_healthy(void) {
    return medicbot::getStorage().isHealthy();
}

}
