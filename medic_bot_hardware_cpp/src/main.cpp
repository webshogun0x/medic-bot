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
    medicbot::getDisplay().sendBootProgress(20, "Dual-Core ESP32-S3 Initialized", 2, 0, 0, 0);

    // 4. Initialize SD Card and SQLite storage
    medicbot::getStorage().begin();
    medicbot::getDisplay().sendBootProgress(40, "SD Card & SQLite Database Ready", 2, 0, 0, 0);

    // 5. Initialize Wi-Fi station and ESP-NOW mesh
    medicbot::getWifi().begin();
    medicbot::getEspNow().begin();
    medicbot::getDisplay().sendBootProgress(65, "Connecting Wi-Fi & ESP-NOW Mesh...", 2, 1, 0, 0);

    // 6. Initialize AS608 Fingerprint & MFRC522 RFID
    medicbot::getFingerprintSensor().begin(PIN_FP_TX, PIN_FP_RX, FP_BAUDRATE);
    mfrc522_init();

    // 7. Initialize MAX30102 Pulse Oximeter
    max30102_init();
    medicbot::getDisplay().sendBootProgress(85, "Biometric & Medical Sensors Ready", 2, 1, 0, 2);

    // 8. Initialize Cloud Sync
    medicbot::getCloudSync().begin();

    bool wifi_ok = medicbot::getWifi().isConnected();
    bool fb_ok = medicbot::getCloudSync().isReady();
    medicbot::getDisplay().sendBootProgress(100, "All Subsystems Connected & Ready", 2, wifi_ok ? 2 : 1, fb_ok ? 2 : 1, 2);

    // 9. Launch Kiosk Orchestrator
    medicbot::getOrchestrator().begin();

    // 10. Secondary instructional voice track
    medicbot::getVoice().play(medicbot::VoiceTrack::INSTRUCTIONS);

    ESP_LOGI(TAG, "Initialization complete. Kiosk Orchestrator active.");
}
