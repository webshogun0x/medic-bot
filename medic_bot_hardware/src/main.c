#include <stdio.h>
#include <string.h>
#include <time.h>
#include "app_config.h"
#include "system_events.h"
#include "user_types.h"
#include "display_comm.h"
#include "mfrc522.h"
#include "as608.h"
#include "max30102.h"
#include "wifi_manager.h"
#include "espnow_manager.h"
#include "storage_manager.h"
#include "cloud_sync.h"
#include "voice_guidance.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "MAIN_APP";

// Global FreeRTOS queues
QueueHandle_t g_sys_event_queue = NULL;
QueueHandle_t g_display_tx_queue = NULL;
QueueHandle_t g_storage_queue = NULL;
QueueHandle_t g_cloud_queue = NULL;

// Bus mutexes
SemaphoreHandle_t g_spi2_mutex = NULL;
SemaphoreHandle_t g_spi3_mutex = NULL;
SemaphoreHandle_t g_i2c_mutex = NULL;

// System state and cache
SemaphoreHandle_t g_state_mutex = NULL;
system_state_t g_current_state = SYS_STATE_INIT;
user_profile_t g_current_user = {0};
vital_readings_t g_current_vitals = {0};
bool g_wifi_connected = false;
bool g_firebase_ready = false;

static TaskHandle_t s_fp_task_handle = NULL;
static volatile bool s_fp_task_running = false;
static volatile bool s_fp_task_abort = false;

static uint8_t rfid_hash_to_fp_id(const char *rfid) {
    if (!rfid) return 1;
    uint32_t hash = 0x811C9DC5;
    for (size_t i = 0; i < strlen(rfid); i++) {
        hash ^= (uint32_t)rfid[i];
        hash *= 0x01000193;
    }
    return (uint8_t)((hash % FP_MAX_SLOTS) + 1);
}

// Fingerprint verification task (non-blocking)
static void fp_verify_worker(void *arg) {
    uint8_t expected_id = (uint8_t)(uintptr_t)arg;
    ESP_LOGI(TAG, "FP verification task started, looking for ID #%d", expected_id);

    int timeout_sec = 15;
    bool matched = false;

    while (timeout_sec > 0 && !s_fp_task_abort) {
        uint16_t matched_id = 0;
        uint16_t score = 0;

        uint8_t img_res = as608_get_image();
        if (img_res == AS608_OK) {
            uint8_t tz_res = as608_image_to_template(1);
            if (tz_res == AS608_OK) {
                uint8_t search_res = as608_fast_search(1, &matched_id, &score);
                if (search_res == AS608_OK) {
                    ESP_LOGI(TAG, "Fingerprint matched ID #%d (Score: %d)", matched_id, score);
                    matched = true;
                    if (g_sys_event_queue) {
                        sys_event_t evt;
                        memset(&evt, 0, sizeof(evt));
                        evt.type = EVT_FP_MATCH_SUCCESS;
                        evt.payload.fp_id = (uint8_t)matched_id;
                        xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(50));
                    }
                    break;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(300));
        timeout_sec--;
    }

    if (!matched && !s_fp_task_abort) {
        ESP_LOGW(TAG, "Fingerprint verification timed out or failed");
        if (g_sys_event_queue) {
            sys_event_t evt = { .type = EVT_FP_MATCH_FAILED };
            xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(50));
        }
    }

    s_fp_task_running = false;
    s_fp_task_handle = NULL;
    vTaskDelete(NULL);
}

// Fingerprint enrollment task (non-blocking 2-step capture)
static void fp_enroll_worker(void *arg) {
    uint8_t slot_id = (uint8_t)(uintptr_t)arg;
    ESP_LOGI(TAG, "FP enrollment task started for slot #%d", slot_id);

    display_send_prompt("Place finger on sensor (Scan 1/2)...");

    // Step 1: First capture
    int timeout = 20;
    bool step1_ok = false;
    while (timeout-- > 0 && !s_fp_task_abort) {
        if (as608_get_image() == AS608_OK) {
            if (as608_image_to_template(1) == AS608_OK) {
                step1_ok = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    if (!step1_ok || s_fp_task_abort) {
        display_send_typed("ENROLL_FAILED", "Scan 1 timed out");
        s_fp_task_running = false;
        s_fp_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    display_send_prompt("Remove finger...");
    vTaskDelay(pdMS_TO_TICKS(1500));

    display_send_prompt("Place same finger again (Scan 2/2)...");

    // Step 2: Second capture
    timeout = 20;
    bool step2_ok = false;
    while (timeout-- > 0 && !s_fp_task_abort) {
        if (as608_get_image() == AS608_OK) {
            if (as608_image_to_template(2) == AS608_OK) {
                step2_ok = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    if (!step2_ok || s_fp_task_abort) {
        display_send_typed("ENROLL_FAILED", "Scan 2 timed out");
        s_fp_task_running = false;
        s_fp_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Combine and store model
    if (as608_create_model() == AS608_OK && as608_store_model(1, slot_id) == AS608_OK) {
        ESP_LOGI(TAG, "Fingerprint enrolled and stored in slot #%d", slot_id);
        if (g_sys_event_queue) {
            sys_event_t evt;
            memset(&evt, 0, sizeof(evt));
            evt.type = EVT_FP_ENROLL_DONE;
            evt.payload.fp_id = slot_id;
            xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(50));
        }
    } else {
        ESP_LOGE(TAG, "Failed to create model or store in slot #%d", slot_id);
        if (g_sys_event_queue) {
            sys_event_t evt = { .type = EVT_FP_ENROLL_FAILED };
            xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(50));
        }
    }

    s_fp_task_running = false;
    s_fp_task_handle = NULL;
    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "=== MEDIC-BOT FIRMWARE STARTING (ESP-IDF / FreeRTOS) ===");

    // 1. Create FreeRTOS queues
    g_sys_event_queue = xQueueCreate(QUEUE_SYS_EVENT_DEPTH, sizeof(sys_event_t));
    g_display_tx_queue = xQueueCreate(QUEUE_DISPLAY_TX_DEPTH, sizeof(display_msg_t));
    g_storage_queue = xQueueCreate(QUEUE_STORAGE_DEPTH, sizeof(storage_cmd_t));
    g_cloud_queue = xQueueCreate(QUEUE_CLOUD_DEPTH, sizeof(cloud_cmd_t));

    // 2. Create mutexes
    g_state_mutex = xSemaphoreCreateMutex();
    g_spi2_mutex = xSemaphoreCreateMutex();
    g_spi3_mutex = xSemaphoreCreateMutex();
    g_i2c_mutex = xSemaphoreCreateMutex();

    // 3. Initialize voice guidance first (audio instructions start immediately)
    voice_guidance_init();
    voice_guidance_play(VOICE_TRACK_INIT);

    // 4. Initialize display UART communication (TX/RX tasks)
    display_comm_init();

    // 5. Initialize SD card & SQLite storage (Core 0 task)
    storage_manager_init();

    // 6. Initialize WiFi station & SNTP time synchronization
    wifi_manager_init(NULL, NULL);

    // 7. Initialize ESP-NOW protocol for wireless sensor reception
    espnow_manager_init();

    // 8. Initialize biometric & RFID sensors
    as608_init();
    mfrc522_init();

    // 9. Initialize MAX30102 pulse oximeter
    max30102_init();

    // 10. Initialize cloud sync background task
    cloud_sync_init();

    // Play secondary instructional voice track non-blocking
    voice_guidance_play(VOICE_TRACK_INSTRUCTIONS);

    g_current_state = SYS_STATE_IDLE;
    ESP_LOGI(TAG, "System initialization complete. Entering main FSM event loop.");

    sys_event_t evt;

    while (1) {
        if (xQueueReceive(g_sys_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            switch (evt.type) {
                case EVT_DISPLAY_READY: {
                    char ip_buf[16] = "0.0.0.0";
                    bool wifi_ok = wifi_manager_is_connected();
                    if (wifi_ok) wifi_manager_get_ip(ip_buf, sizeof(ip_buf));
                    display_send_status("Ready", wifi_ok, ip_buf, cloud_sync_is_ready());
                    break;
                }

                case EVT_CMD_START_LOGIN:
                    if (g_current_state == SYS_STATE_IDLE) {
                        g_current_state = SYS_STATE_LOGIN_WAIT_RFID;
                        display_send_prompt("Please scan your RFID card to log in...");
                        ESP_LOGI(TAG, "State -> SYS_STATE_LOGIN_WAIT_RFID");
                    }
                    break;

                case EVT_CMD_START_ENROLLMENT:
                    if (g_current_state == SYS_STATE_IDLE) {
                        g_current_state = SYS_STATE_ENROLL_WAIT_RFID;
                        display_send_prompt("Please scan your registered RFID card...");
                        ESP_LOGI(TAG, "State -> SYS_STATE_ENROLL_WAIT_RFID");
                    }
                    break;

                case EVT_CMD_BACK:
                    ESP_LOGI(TAG, "User clicked BACK - cancelling active operation");
                    if (s_fp_task_running) {
                        s_fp_task_abort = true;
                    }
                    max30102_cancel_measurement();
                    g_current_state = SYS_STATE_IDLE;
                    display_send_prompt("Cancelled. Welcome to MediBot.");
                    break;

                case EVT_CMD_LOGOUT:
                    ESP_LOGI(TAG, "User logged out");
                    g_current_state = SYS_STATE_IDLE;
                    memset(&g_current_user, 0, sizeof(user_profile_t));
                    memset(&g_current_vitals, 0, sizeof(vital_readings_t));
                    display_send_prompt("Logged out successfully.");
                    break;

                case EVT_RFID_SCANNED: {
                    ESP_LOGI(TAG, "RFID scanned: [%s] in state %d", evt.payload.rfid, g_current_state);
                    if (g_current_state == SYS_STATE_LOGIN_WAIT_RFID) {
                        user_profile_t profile;
                        bool found = storage_manager_get_user(evt.payload.rfid, &profile);

                        if (found) {
                            g_current_user = profile;
                            if (profile.fingerprint_registered && profile.fingerprint_id > 0) {
                                g_current_state = SYS_STATE_LOGIN_WAIT_FP;
                                display_send_prompt("RFID verified. Place finger on sensor...");

                                s_fp_task_abort = false;
                                s_fp_task_running = true;
                                xTaskCreatePinnedToCore(
                                    fp_verify_worker, "fp_verify", STACK_FINGERPRINT,
                                    (void *)(uintptr_t)profile.fingerprint_id,
                                    PRIO_FINGERPRINT, &s_fp_task_handle, CORE_REALTIME_APP
                                );
                            } else {
                                display_send_typed("FINGERPRINT_ERROR", "No fingerprint enrolled. Please enroll first.");
                                g_current_state = SYS_STATE_IDLE;
                            }
                        } else {
                            display_send_prompt("User not found locally. Checking cloud...");
                            cloud_sync_queue_fetch_user(evt.payload.rfid);
                        }
                    } else if (g_current_state == SYS_STATE_ENROLL_WAIT_RFID) {
                        strncpy(g_current_user.rfid, evt.payload.rfid, sizeof(g_current_user.rfid) - 1);
                        uint8_t assigned_id = rfid_hash_to_fp_id(evt.payload.rfid);
                        g_current_user.fingerprint_id = assigned_id;

                        g_current_state = SYS_STATE_ENROLL_SCAN_FP1;
                        s_fp_task_abort = false;
                        s_fp_task_running = true;
                        xTaskCreatePinnedToCore(
                            fp_enroll_worker, "fp_enroll", STACK_FINGERPRINT,
                            (void *)(uintptr_t)assigned_id,
                            PRIO_FINGERPRINT, &s_fp_task_handle, CORE_REALTIME_APP
                        );
                    }
                    break;
                }

                case EVT_USER_LOADED:
                    g_current_user = evt.payload.user;
                    if (g_current_state == SYS_STATE_LOGIN_WAIT_RFID) {
                        storage_manager_save_user(&g_current_user);
                        if (g_current_user.fingerprint_registered && g_current_user.fingerprint_id > 0) {
                            g_current_state = SYS_STATE_LOGIN_WAIT_FP;
                            display_send_prompt("Profile loaded. Place finger on sensor...");
                            s_fp_task_abort = false;
                            s_fp_task_running = true;
                            xTaskCreatePinnedToCore(
                                fp_verify_worker, "fp_verify", STACK_FINGERPRINT,
                                (void *)(uintptr_t)g_current_user.fingerprint_id,
                                PRIO_FINGERPRINT, &s_fp_task_handle, CORE_REALTIME_APP
                            );
                        } else {
                            display_send_typed("FINGERPRINT_ERROR", "No fingerprint registered for this card.");
                            g_current_state = SYS_STATE_IDLE;
                        }
                    }
                    break;

                case EVT_USER_NOT_FOUND:
                    display_send_prompt("Card not recognized. Please register on Web App first.");
                    g_current_state = SYS_STATE_IDLE;
                    break;

                case EVT_FP_MATCH_SUCCESS:
                    g_current_user.is_logged_in = true;
                    g_current_state = SYS_STATE_DASHBOARD;
                    display_send_typed("FINGERPRINT_SUCCESS", "Authentication successful!");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    display_send_user_data(&g_current_user);
                    break;

                case EVT_FP_MATCH_FAILED:
                    display_send_typed("FINGERPRINT_ERROR", "Fingerprint mismatch. Try again.");
                    g_current_state = SYS_STATE_IDLE;
                    break;

                case EVT_FP_ENROLL_DONE: {
                    uint8_t fp_id = evt.payload.fp_id;
                    g_current_user.fingerprint_registered = true;
                    g_current_user.fingerprint_id = fp_id;

                    storage_manager_update_fingerprint(g_current_user.rfid, fp_id);
                    cloud_sync_queue_update_fp(g_current_user.rfid, fp_id);

                    display_send_prompt("Enrollment complete! Logging into Dashboard...");
                    g_current_user.is_logged_in = true;
                    g_current_state = SYS_STATE_DASHBOARD;
                    vTaskDelay(pdMS_TO_TICKS(1500));
                    display_send_user_data(&g_current_user);
                    break;
                }

                case EVT_FP_ENROLL_FAILED:
                    display_send_prompt("Enrollment failed. Please try again.");
                    g_current_state = SYS_STATE_IDLE;
                    break;

                case EVT_CMD_READ_OXIMETER:
                    display_send_prompt("Measuring vitals... Place finger on oximeter.");
                    max30102_start_measurement();
                    espnow_manager_request_data();
                    break;

                case EVT_OXIMETER_DONE:
                    g_current_vitals.heart_rate = evt.payload.oximeter.hr;
                    g_current_vitals.spo2 = evt.payload.oximeter.spo2;
                    g_current_vitals.temperature = evt.payload.oximeter.temperature;
                    display_send_sensor_data(&g_current_vitals);
                    display_send_prompt("Oximeter reading complete.");
                    break;

                case EVT_OXIMETER_NO_FINGER:
                    display_send_prompt("No finger detected on sensor. Try again.");
                    break;

                case EVT_HW_DATA_RECEIVED:
                    g_current_vitals.weight = evt.payload.hw_data.weight_kg;
                    g_current_vitals.height_laser = evt.payload.hw_data.height_lidar_cm / 100.0f;
                    g_current_vitals.height_sonar = evt.payload.hw_data.height_sonar_cm / 100.0f;
                    g_current_vitals.bmi_laser = evt.payload.hw_data.bmi_lidar;
                    g_current_vitals.bmi_sonar = evt.payload.hw_data.bmi_sonar;
                    display_send_sensor_data(&g_current_vitals);
                    break;

                case EVT_CMD_SAVE_READINGS:
                    strncpy(g_current_vitals.rfid, g_current_user.rfid, sizeof(g_current_vitals.rfid) - 1);
                    storage_cmd_t store_cmd = {
                        .type = STORE_CMD_SAVE_READING,
                        .data.reading = g_current_vitals,
                    };
                    xQueueSend(g_storage_queue, &store_cmd, pdMS_TO_TICKS(50));
                    cloud_sync_queue_reading(&g_current_vitals);
                    display_send_prompt("Vitals saved to database and synced to cloud!");
                    break;

                case EVT_WIFI_CONNECTED: {
                    char ip[16] = {0};
                    wifi_manager_get_ip(ip, sizeof(ip));
                    display_send_status("WiFi Connected", true, ip, cloud_sync_is_ready());
                    break;
                }

                case EVT_WIFI_DISCONNECTED:
                    display_send_status("WiFi Disconnected", false, "0.0.0.0", false);
                    break;

                default:
                    break;
            }
        }
    }
}