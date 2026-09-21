#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "user_types.h"

#ifdef __cplusplus
namespace medicbot {

class CloudSync {
public:
    CloudSync();
    ~CloudSync();

    esp_err_t begin();
    esp_err_t queueReading(const vital_readings_t *reading);
    esp_err_t queueFetchUser(const char *rfid);
    esp_err_t queueUpdateFp(const char *rfid, uint8_t fp_id);
    bool isReady() const { return m_ready; }
    bool isConfigured() const;
    void testConnection();
    const char *getFirebaseHost() const;

private:
    bool m_ready;
    static void workerTaskTrampoline(void *arg);
    void workerTask();
};

CloudSync &getCloudSync();

} // namespace medicbot

extern "C" {
#endif

esp_err_t cloud_sync_init(void);
esp_err_t cloud_sync_queue_reading(const vital_readings_t *reading);
esp_err_t cloud_sync_queue_fetch_user(const char *rfid);
esp_err_t cloud_sync_queue_update_fp(const char *rfid, uint8_t fp_id);
bool cloud_sync_is_ready(void);

#ifdef __cplusplus
}
#endif
