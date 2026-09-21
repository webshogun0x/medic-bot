#pragma once

#include "user_types.h"
#include "system_events.h"
#include "esp_err.h"

#ifdef __cplusplus
namespace medicbot {

class StorageManager {
public:
    StorageManager();
    ~StorageManager();

    esp_err_t begin();
    bool getUser(const char *rfid, user_profile_t &out_user);
    bool saveUser(const user_profile_t &user);
    bool saveReading(const vital_readings_t &reading);
    bool updateFingerprint(const char *rfid, uint8_t fp_id);
    bool isHealthy() const { return m_db_ready; }

private:
    bool m_db_ready;
    void *m_db; // sqlite3 *
    SemaphoreHandle_t m_db_mutex;

    void initTables();
    bool executeSql(const char *sql);
    static void workerTaskTrampoline(void *arg);
    void workerTask();
};

StorageManager &getStorage();

} // namespace medicbot

extern "C" {
#endif

esp_err_t storage_manager_init(void);
bool storage_manager_get_user(const char *rfid, user_profile_t *out_user);
bool storage_manager_save_user(const user_profile_t *user);
bool storage_manager_save_reading(const vital_readings_t *reading);
bool storage_manager_update_fingerprint(const char *rfid, uint8_t fp_id);
bool storage_manager_is_healthy(void);

#ifdef __cplusplus
}
#endif
