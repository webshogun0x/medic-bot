#include <cstdio>
#include <cstring>
#include "app_config.h"
#include "system_events.h"
#include "orchestrator.hpp"
#include "display_comm.hpp"
#include "as608.hpp"
#include "espnow_manager.hpp"
#include "storage_manager.hpp"
#include "wifi_manager.hpp"
#include "cloud_sync.hpp"
#include "voice_guidance.hpp"
#include "max30102.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "MAIN_CPP";

// Global FreeRTOS queues
QueueHandle_t g_sys_event_queue = nullptr;
QueueHandle_t g_display_tx_queue = nullptr;
QueueHandle_t g_storage_queue = nullptr;
QueueHandle_t g_cloud_queue = nullptr;

extern "C" esp_err_t mfrc522_init(void);

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  MEDIC-BOT HEALTH KIOSK — ESP-IDF C++ MAIN CONTROLLER   ");
    ESP_LOGI(TAG, "==========================================================");

    // 1. Create FreeRTOS inter-task communication queues
    g_sys_event_queue = xQueueCreate(QUEUE_SYS_EVENT_DEPTH, sizeof(sys_event_t));
    g_display_tx_queue = xQueueCreate(QUEUE_DISPLAY_TX_DEPTH, 256);
    g_storage_queue = xQueueCreate(QUEUE_STORAGE_DEPTH, sizeof(storage_cmd_t));
    g_cloud_queue = xQueueCreate(QUEUE_CLOUD_DEPTH, sizeof(cloud_cmd_t));

    // 2. Initialize Voice Guidance audio subsystem
    medicbot::getVoice().begin();
    medicbot::getVoice().play(medicbot::VoiceTrack::INIT);

    // 3. Initialize Sunton Display UART communication
    medicbot::getDisplay().begin(PIN_DISPLAY_TX, PIN_DISPLAY_RX, DISPLAY_BAUDRATE);
    medicbot::getDisplay().sendBootProgress(15, "Dual-Core ESP32-S3 Initialized", 2, 0, 0, 0);

    // 4. Initialize SD Card and SQLite storage FIRST (mounts /sdcard/wifi.txt)
    medicbot::getStorage().begin();
    bool storage_ok = medicbot::getStorage().isHealthy();
    medicbot::getDisplay().sendBootProgress(35, storage_ok ? "SD Card & SQLite Database Ready" : "Local Storage Warning (Using Flash)", 2, 0, 0, 0);

    // 5. Initialize Wi-Fi station (reads /sdcard/wifi.txt) & ESP-NOW mesh
    medicbot::getWifi().begin();
    medicbot::getEspNow().begin();

    // 6. Initialize AS608 Fingerprint, MFRC522 RFID, and MAX30102 Oximeter
    medicbot::getFingerprintSensor().begin(PIN_FP_TX, PIN_FP_RX, FP_BAUDRATE);
    mfrc522_init();
    max30102_init();
    medicbot::getDisplay().sendBootProgress(60, "Hardware Sensors & ESP-NOW Ready", 2, 1, 0, 2);

    // 7. Verify Wi-Fi station IP assignment (polling loop)
    bool wifi_connected = false;
    int wifi_wait_half_sec = 0;
    const int max_wifi_wait_half_sec = 24; // 12 seconds max timeout

    while (wifi_wait_half_sec < max_wifi_wait_half_sec) {
        if (medicbot::getWifi().isConnected()) {
            wifi_connected = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        wifi_wait_half_sec++;
        char progress_msg[64];
        snprintf(progress_msg, sizeof(progress_msg), "Connecting to Wi-Fi (%d/%ds)...", (wifi_wait_half_sec + 1) / 2, max_wifi_wait_half_sec / 2);
        int pct = 60 + (wifi_wait_half_sec * 20 / max_wifi_wait_half_sec);
        medicbot::getDisplay().sendBootProgress(pct, progress_msg, 2, 1, 0, 2);
    }

    // 7. Initialize Cloud Sync and test Firebase Realtime Database
    medicbot::getCloudSync().begin();
    bool fb_connected = false;

    if (wifi_connected) {
        char ip_buf[16] = {0};
        medicbot::getWifi().getIp(ip_buf, sizeof(ip_buf));
        ESP_LOGI(TAG, "Wi-Fi Connected! IP: %s", ip_buf);
        medicbot::getDisplay().sendBootProgress(85, "Verifying Firebase Cloud Database...", 2, 2, 1, 2);
        fb_connected = medicbot::getCloudSync().testConnection();
        if (fb_connected) {
            medicbot::getDisplay().sendBootProgress(95, "Cloud Database Verified Online", 2, 2, 2, 2);
        } else {
            medicbot::getDisplay().sendBootProgress(95, "Cloud Database Unreachable (Local Mode)", 2, 2, 0, 2);
        }
    } else {
        ESP_LOGW(TAG, "Wi-Fi connection timed out. Booting in Local Storage / Offline Mode.");
        medicbot::getDisplay().sendBootProgress(95, "Wi-Fi Offline (Local Storage Mode)", 2, 0, 0, 2);
    }

    // 8. Final 100% Boot Progress & Status
    vTaskDelay(pdMS_TO_TICKS(500));
    char ip_buf[16] = "0.0.0.0";
    if (wifi_connected) {
        medicbot::getWifi().getIp(ip_buf, sizeof(ip_buf));
    }
    medicbot::getDisplay().sendBootProgress(100, "All Subsystems Verified & Ready", 2, wifi_connected ? 2 : 0, fb_connected ? 2 : 0, 2);
    medicbot::getDisplay().sendStatus("Ready", wifi_connected, ip_buf, fb_connected);

    // 9. Launch Kiosk Orchestrator
    medicbot::getOrchestrator().begin();

    // 10. Secondary instructional voice track
    medicbot::getVoice().play(medicbot::VoiceTrack::INSTRUCTIONS);

    ESP_LOGI(TAG, "Initialization complete. Kiosk Orchestrator active.");
}
