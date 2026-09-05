#include "storage_manager.h"
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
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "STORAGE_MGR";

static sqlite3 *s_db = NULL;
static bool s_db_ready = false;
static sdmmc_card_t *s_card = NULL;
static SemaphoreHandle_t s_db_mutex = NULL;

static bool execute_sql(const char *sql) {
    if (!s_db) return false;
    char *err_msg = NULL;
    int rc = sqlite3_exec(s_db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "SQL exec error: %s", err_msg ? err_msg : "unknown");
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

static void init_tables(void) {
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
    execute_sql(users_table_sql);

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
    execute_sql(readings_table_sql);

    execute_sql("CREATE INDEX IF NOT EXISTS idx_readings_rfid ON health_readings(rfid);");
    execute_sql("CREATE INDEX IF NOT EXISTS idx_readings_timestamp ON health_readings(timestamp);");
    execute_sql("CREATE INDEX IF NOT EXISTS idx_readings_synced ON health_readings(synced_to_firebase);");
}

bool storage_manager_get_user(const char *rfid, user_profile_t *out_user) {
    if (!s_db_ready || !rfid || !out_user) return false;

    xSemaphoreTake(s_db_mutex, portMAX_DELAY);
    const char *sql = "SELECT rfid, name, first_name, last_name, email, age, gender, medical_id, "
                      "fingerprint_registered, fingerprint_id FROM users WHERE rfid = ?;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        xSemaphoreGive(s_db_mutex);
        return false;
    }

    sqlite3_bind_text(stmt, 1, rfid, -1, SQLITE_STATIC);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        memset(out_user, 0, sizeof(user_profile_t));
        const char *t;
        t = (const char *)sqlite3_column_text(stmt, 0); if (t) strncpy(out_user->rfid, t, sizeof(out_user->rfid) - 1);
        t = (const char *)sqlite3_column_text(stmt, 1); if (t) strncpy(out_user->name, t, sizeof(out_user->name) - 1);
        t = (const char *)sqlite3_column_text(stmt, 2); if (t) strncpy(out_user->first_name, t, sizeof(out_user->first_name) - 1);
        t = (const char *)sqlite3_column_text(stmt, 3); if (t) strncpy(out_user->last_name, t, sizeof(out_user->last_name) - 1);
        t = (const char *)sqlite3_column_text(stmt, 4); if (t) strncpy(out_user->email, t, sizeof(out_user->email) - 1);
        t = (const char *)sqlite3_column_text(stmt, 5); if (t) strncpy(out_user->age, t, sizeof(out_user->age) - 1);
        t = (const char *)sqlite3_column_text(stmt, 6); if (t) strncpy(out_user->gender, t, sizeof(out_user->gender) - 1);
        t = (const char *)sqlite3_column_text(stmt, 7); if (t) strncpy(out_user->medical_id, t, sizeof(out_user->medical_id) - 1);
        out_user->fingerprint_registered = (sqlite3_column_int(stmt, 8) != 0);
        out_user->fingerprint_id = (uint8_t)sqlite3_column_int(stmt, 9);
        found = true;
    }

    sqlite3_finalize(stmt);

    if (found) {
        // Increment login count
        const char *update_sql = "UPDATE users SET last_login = CURRENT_TIMESTAMP, login_count = login_count + 1 WHERE rfid = ?;";
        if (sqlite3_prepare_v2(s_db, update_sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, rfid, -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    xSemaphoreGive(s_db_mutex);
    return found;
}

bool storage_manager_save_user(const user_profile_t *user) {
    if (!s_db_ready || !user) return false;

    xSemaphoreTake(s_db_mutex, portMAX_DELAY);
    const char *sql =
        "INSERT OR REPLACE INTO users "
        "(rfid, name, first_name, last_name, email, age, gender, medical_id, fingerprint_registered, fingerprint_id, login_count) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, COALESCE((SELECT login_count FROM users WHERE rfid = ?), 0));";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        xSemaphoreGive(s_db_mutex);
        return false;
    }

    sqlite3_bind_text(stmt, 1, user->rfid, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, user->first_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, user->last_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, user->email, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, user->age, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, user->gender, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, user->medical_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 9, user->fingerprint_registered ? 1 : 0);
    sqlite3_bind_int(stmt, 10, user->fingerprint_id);
    sqlite3_bind_text(stmt, 11, user->rfid, -1, SQLITE_STATIC);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    xSemaphoreGive(s_db_mutex);

    if (ok) {
        ESP_LOGI(TAG, "Saved user profile [%s] to SQLite", user->rfid);
    }
    return ok;
}

bool storage_manager_save_reading(const vital_readings_t *r) {
    if (!s_db_ready || !r) return false;

    xSemaphoreTake(s_db_mutex, portMAX_DELAY);
    const char *sql =
        "INSERT INTO health_readings "
        "(rfid, timestamp, heart_rate, spo2, temperature, weight, height, bmi_sonar, bmi_laser, systolic, diastolic, synced_to_firebase) "
        "VALUES (?, datetime('now'), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        xSemaphoreGive(s_db_mutex);
        return false;
    }

    sqlite3_bind_text(stmt, 1, r->rfid, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 2, r->heart_rate);
    sqlite3_bind_double(stmt, 3, r->spo2);
    sqlite3_bind_double(stmt, 4, r->temperature);
    sqlite3_bind_double(stmt, 5, r->weight);
    sqlite3_bind_double(stmt, 6, r->height_laser);
    sqlite3_bind_double(stmt, 7, r->bmi_sonar);
    sqlite3_bind_double(stmt, 8, r->bmi_laser);
    sqlite3_bind_int(stmt, 9, r->systolic);
    sqlite3_bind_int(stmt, 10, r->diastolic);
    sqlite3_bind_int(stmt, 11, r->synced_to_firebase ? 1 : 0);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    xSemaphoreGive(s_db_mutex);

    if (ok) {
        ESP_LOGI(TAG, "Saved health reading for RFID [%s]", r->rfid);
    }
    return ok;
}

bool storage_manager_update_fingerprint(const char *rfid, uint8_t fp_id) {
    if (!s_db_ready || !rfid) return false;

    xSemaphoreTake(s_db_mutex, portMAX_DELAY);
    const char *sql = "UPDATE users SET fingerprint_registered = 1, fingerprint_id = ? WHERE rfid = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        xSemaphoreGive(s_db_mutex);
        return false;
    }

    sqlite3_bind_int(stmt, 1, fp_id);
    sqlite3_bind_text(stmt, 2, rfid, -1, SQLITE_STATIC);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    xSemaphoreGive(s_db_mutex);
    return ok;
}

static void storage_worker_task(void *pvParameters) {
    ESP_LOGI(TAG, "Storage worker task started on Core %d", xPortGetCoreID());
    storage_cmd_t cmd;

    while (1) {
        if (xQueueReceive(g_storage_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.type) {
                case STORE_CMD_LOAD_USER: {
                    user_profile_t profile;
                    bool found = storage_manager_get_user(cmd.data.rfid, &profile);
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
                    storage_manager_save_user(&cmd.data.user);
                    break;

                case STORE_CMD_SAVE_READING: {
                    bool saved = storage_manager_save_reading(&cmd.data.reading);
                    if (saved && g_sys_event_queue) {
                        sys_event_t evt = { .type = EVT_READINGS_SAVED };
                        xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(50));
                    }
                    break;
                }
                case STORE_CMD_UPDATE_FP:
                    storage_manager_update_fingerprint(cmd.data.fp_update.rfid, cmd.data.fp_update.fp_id);
                    break;

                default:
                    break;
            }
        }
    }
}

esp_err_t storage_manager_init(void) {
    if (!s_db_mutex) {
        s_db_mutex = xSemaphoreCreateMutex();
    }

    if (!g_storage_queue) {
        g_storage_queue = xQueueCreate(QUEUE_STORAGE_DEPTH, sizeof(storage_cmd_t));
    }

    ESP_LOGI(TAG, "Mounting SD Card on SPI3 (MISO:%d, MOSI:%d, SCK:%d, CS:%d)...",
             PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_SCK, PIN_SD_CS);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_SD_MOSI,
        .miso_io_num = PIN_SD_MISO,
        .sclk_io_num = PIN_SD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus for SD Card: %s", esp_err_to_name(ret));
        return ret;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev_cfg.gpio_cs = PIN_SD_CS;
    dev_cfg.host_id = SD_SPI_HOST;

    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &dev_cfg, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD Card mount failed: %s (Continuing in fallback mode)", esp_err_to_name(ret));
        // Still open an in-memory database as fallback so system operates
        sqlite3_initialize();
        if (sqlite3_open(":memory:", &s_db) == SQLITE_OK) {
            init_tables();
            s_db_ready = true;
            ESP_LOGI(TAG, "In-memory SQLite database fallback initialized");
        }
    } else {
        ESP_LOGI(TAG, "SD Card mounted at %s (%llu MB)", SD_MOUNT_POINT,
                 ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024));

        sqlite3_initialize();
        if (sqlite3_open(SD_DB_PATH, &s_db) == SQLITE_OK) {
            init_tables();
            s_db_ready = true;
            ESP_LOGI(TAG, "SQLite database ready on SD Card (%s)", SD_DB_PATH);
        } else {
            ESP_LOGE(TAG, "Failed to open SQLite database file on SD Card");
        }
    }

    xTaskCreatePinnedToCore(
        storage_worker_task,
        "storage_task",
        STACK_STORAGE,
        NULL,
        PRIO_STORAGE,
        NULL,
        CORE_NETWORK_STORAGE
    );

    return ESP_OK;
}

bool storage_manager_is_healthy(void) {
    return s_db_ready;
}
