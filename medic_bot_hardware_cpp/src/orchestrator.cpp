#include "orchestrator.hpp"
#include "app_config.h"
#include "as608.hpp"
#include "display_comm.hpp"
#include "espnow_manager.hpp"
#include "storage_manager.hpp"
#include "wifi_manager.hpp"
#include "cloud_sync.hpp"
#include "voice_guidance.hpp"
#include "max30102.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdio>

static const char *TAG = "ORCHESTRATOR_CPP";

namespace medicbot {

static KioskOrchestrator s_orchestrator_instance;

KioskOrchestrator &getOrchestrator() {
    return s_orchestrator_instance;
}

static TaskHandle_t s_fp_task = nullptr;
static volatile bool s_fp_abort = false;
static volatile bool s_fp_running = false;

static uint8_t rfid_to_slot(const char *rfid) {
    if (!rfid) return 1;
    uint32_t hash = 0x811C9DC5;
    for (size_t i = 0; i < strlen(rfid); i++) {
        hash ^= static_cast<uint32_t>(rfid[i]);
        hash *= 0x01000193;
    }
    return static_cast<uint8_t>((hash % FP_MAX_SLOTS) + 1);
}

// Fingerprint verification worker (FreeRTOS task)
static void fp_verify_task(void *arg) {
    uint8_t expected_slot = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(arg));
    ESP_LOGI(TAG, "FP verification started for expected slot #%d", expected_slot);

    int timeout_sec = 15;
    bool matched = false;

    while (timeout_sec > 0 && !s_fp_abort) {
        uint16_t matched_id = 0;
        uint16_t score = 0;

        if (getFingerprintSensor().getImage() == FINGERPRINT_OK) {
            if (getFingerprintSensor().image2Tz(1) == FINGERPRINT_OK) {
                if (getFingerprintSensor().fingerFastSearch(1, matched_id, score) == FINGERPRINT_OK) {
                    ESP_LOGI(TAG, "FP matched ID #%d (Score: %d)", matched_id, score);
                    matched = true;
                    if (g_sys_event_queue) {
                        sys_event_t evt;
                        memset(&evt, 0, sizeof(evt));
                        evt.type = EVT_FP_MATCH_SUCCESS;
                        evt.payload.fp_id = static_cast<uint8_t>(matched_id);
                        xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(50));
                    }
                    break;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(300));
        timeout_sec--;
    }

    if (!matched && !s_fp_abort) {
        ESP_LOGW(TAG, "Fingerprint verification failed or timed out");
        if (g_sys_event_queue) {
            sys_event_t evt;
            memset(&evt, 0, sizeof(evt));
            evt.type = EVT_FP_MATCH_FAILED;
            xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(50));
        }
    }

    s_fp_running = false;
    s_fp_task = nullptr;
    vTaskDelete(nullptr);
}

// Fingerprint enrollment worker (2-step capture FreeRTOS task)
static void fp_enroll_task(void *arg) {
    uint8_t slot_id = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(arg));
    ESP_LOGI(TAG, "FP enrollment task started for slot #%d", slot_id);

    getDisplay().sendPrompt("Place finger on sensor (Scan 1/2)...");

    // Scan 1
    int timeout = 20;
    bool step1_ok = false;
    while (timeout-- > 0 && !s_fp_abort) {
        if (getFingerprintSensor().getImage() == FINGERPRINT_OK) {
            if (getFingerprintSensor().image2Tz(1) == FINGERPRINT_OK) {
                step1_ok = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    if (!step1_ok || s_fp_abort) {
        getDisplay().sendTyped("ENROLL_FAILED", "Scan 1 timed out");
        s_fp_running = false;
        s_fp_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    getDisplay().sendPrompt("Remove finger...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    getDisplay().sendPrompt("Place same finger again (Scan 2/2)...");

    // Scan 2
    timeout = 20;
    bool step2_ok = false;
    while (timeout-- > 0 && !s_fp_abort) {
        if (getFingerprintSensor().getImage() == FINGERPRINT_OK) {
            if (getFingerprintSensor().image2Tz(2) == FINGERPRINT_OK) {
                step2_ok = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    if (!step2_ok || s_fp_abort) {
        getDisplay().sendTyped("ENROLL_FAILED", "Scan 2 timed out");
        s_fp_running = false;
        s_fp_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    // Combine models and store in slot
    if (getFingerprintSensor().createModel() == FINGERPRINT_OK &&
        getFingerprintSensor().storeModel(1, slot_id) == FINGERPRINT_OK) {
        ESP_LOGI(TAG, "Fingerprint enrolled and saved in slot #%d", slot_id);
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
            sys_event_t evt;
            memset(&evt, 0, sizeof(evt));
            evt.type = EVT_FP_ENROLL_FAILED;
            xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(50));
        }
    }

    s_fp_running = false;
    s_fp_task = nullptr;
    vTaskDelete(nullptr);
}

KioskOrchestrator::KioskOrchestrator()
    : m_state(OrchestratorState::BOOT) {
    memset(&m_currentUser, 0, sizeof(m_currentUser));
    memset(&m_latestVitals, 0, sizeof(m_latestVitals));
}

KioskOrchestrator::~KioskOrchestrator() {}

void KioskOrchestrator::orchestratorTaskTrampoline(void *arg) {
    static_cast<KioskOrchestrator *>(arg)->orchestratorTask();
}

void KioskOrchestrator::begin() {
    xTaskCreatePinnedToCore(
        orchestratorTaskTrampoline,
        "kiosk_orch_task",
        STACK_FSM,
        this,
        PRIO_ORCHESTRATOR,
        nullptr,
        CORE_REALTIME_APP
    );
}

void KioskOrchestrator::resetToStandby() {
    m_state = OrchestratorState::STANDBY;
    if (s_fp_running) {
        s_fp_abort = true;
    }
    max30102_cancel_measurement();
    getDisplay().sendPrompt("Welcome to MediBot Health Kiosk. Please select an option.");
}

void KioskOrchestrator::handleRfidScanned(const char *uid) {
    if (!uid) return;
    ESP_LOGI(TAG, "RFID scanned: [%s] in state %d", uid, static_cast<int>(m_state));

    if (m_state == OrchestratorState::LOGIN_2FA || m_state == OrchestratorState::STANDBY) {
        user_profile_t profile;
        bool found = getStorage().getUser(uid, profile);

        if (found) {
            m_currentUser = profile;
            if (profile.biometric_enrolled && profile.fingerprint_slot > 0) {
                m_state = OrchestratorState::LOGIN_2FA;
                getDisplay().sendPrompt("RFID verified. Place finger on sensor...");

                s_fp_abort = false;
                s_fp_running = true;
                xTaskCreatePinnedToCore(
                    fp_verify_task, "fp_verify", STACK_FINGERPRINT,
                    reinterpret_cast<void *>(static_cast<uintptr_t>(profile.fingerprint_slot)),
                    PRIO_FINGERPRINT, &s_fp_task, CORE_REALTIME_APP
                );
            } else {
                getDisplay().sendTyped("FINGERPRINT_ERROR", "No fingerprint enrolled. Please enroll first.");
                resetToStandby();
            }
        } else {
            getDisplay().sendPrompt("User not found locally. Querying cloud...");
            getCloudSync().queueFetchUser(uid);
        }
    } else if (m_state == OrchestratorState::NEW_USER_ENROLL) {
        strncpy(m_currentUser.rfid_uid, uid, sizeof(m_currentUser.rfid_uid) - 1);
        uint8_t assigned_slot = rfid_to_slot(uid);
        m_currentUser.fingerprint_slot = assigned_slot;

        s_fp_abort = false;
        s_fp_running = true;
        xTaskCreatePinnedToCore(
            fp_enroll_task, "fp_enroll", STACK_FINGERPRINT,
            reinterpret_cast<void *>(static_cast<uintptr_t>(assigned_slot)),
            PRIO_FINGERPRINT, &s_fp_task, CORE_REALTIME_APP
        );
    }
}

void KioskOrchestrator::executeOrchestratedMeasurement() {
    ESP_LOGI(TAG, "Starting Master Vitals Orchestration Sequence...");
    m_state = OrchestratorState::MEASURING_VITALS;
    getDisplay().sendPrompt("Measuring vitals... Step on scale and place finger on oximeter.");

    memset(&m_latestVitals, 0, sizeof(m_latestVitals));

    // Parallel Pulse Oximeter on console
    max30102_start_measurement();

    // Step 1: Request weight from Weight Scale Node over ESP-NOW
    getEspNow().requestWeight();
}

void KioskOrchestrator::processEvent(const sys_event_t &evt) {
    switch (evt.type) {
        case EVT_DISPLAY_READY: {
            char ip_buf[16] = "0.0.0.0";
            bool wifi_ok = getWifi().isConnected();
            if (wifi_ok) getWifi().getIp(ip_buf, sizeof(ip_buf));
            bool fb_ok = getCloudSync().isReady();
            getDisplay().sendBootProgress(100, "All Subsystems Connected & Ready", 2, wifi_ok ? 2 : 1, fb_ok ? 2 : 1, 2);
            getDisplay().sendStatus("Ready", wifi_ok, ip_buf, fb_ok);
            m_state = OrchestratorState::STANDBY;
            break;
        }

        case EVT_CMD_START_LOGIN:
            m_state = OrchestratorState::LOGIN_2FA;
            getDisplay().sendPrompt("Please scan your RFID card to log in...");
            break;

        case EVT_CMD_START_ENROLLMENT:
            m_state = OrchestratorState::NEW_USER_ENROLL;
            getDisplay().sendPrompt("Please scan your registered RFID card...");
            break;

        case EVT_CMD_BACK:
            ESP_LOGI(TAG, "User clicked BACK - cancelling active operation");
            resetToStandby();
            break;

        case EVT_CMD_LOGOUT:
            ESP_LOGI(TAG, "User logged out");
            memset(&m_currentUser, 0, sizeof(m_currentUser));
            memset(&m_latestVitals, 0, sizeof(m_latestVitals));
            resetToStandby();
            break;

        case EVT_RFID_SCANNED: {
            const char *uid = evt.payload.rfid_uid[0] != '\0' ? evt.payload.rfid_uid : evt.payload.rfid;
            handleRfidScanned(uid);
            break;
        }

        case EVT_USER_LOADED:
            m_currentUser = evt.payload.user;
            getStorage().saveUser(m_currentUser);
            if (m_state == OrchestratorState::LOGIN_2FA || m_state == OrchestratorState::STANDBY) {
                if (m_currentUser.biometric_enrolled && m_currentUser.fingerprint_slot > 0) {
                    m_state = OrchestratorState::LOGIN_2FA;
                    getDisplay().sendPrompt("Profile loaded. Place finger on sensor...");
                    s_fp_abort = false;
                    s_fp_running = true;
                    xTaskCreatePinnedToCore(
                        fp_verify_task, "fp_verify", STACK_FINGERPRINT,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(m_currentUser.fingerprint_slot)),
                        PRIO_FINGERPRINT, &s_fp_task, CORE_REALTIME_APP
                    );
                } else {
                    getDisplay().sendTyped("FINGERPRINT_ERROR", "No fingerprint registered for this card.");
                    resetToStandby();
                }
            }
            break;

        case EVT_USER_NOT_FOUND:
            getDisplay().sendPrompt("Card not recognized. Please register on Web App first.");
            resetToStandby();
            break;

        case EVT_FP_MATCH_SUCCESS:
            m_currentUser.is_logged_in = true;
            m_state = OrchestratorState::DASHBOARD_ACTIVE;
            getDisplay().sendTyped("FINGERPRINT_SUCCESS", "Authentication successful!");
            vTaskDelay(pdMS_TO_TICKS(800));
            getDisplay().sendUserData(&m_currentUser);
            break;

        case EVT_FP_MATCH_FAILED:
            getDisplay().sendTyped("FINGERPRINT_ERROR", "Fingerprint mismatch. Try again.");
            resetToStandby();
            break;

        case EVT_FP_ENROLL_DONE: {
            uint8_t slot_id = evt.payload.fp_id;
            m_currentUser.biometric_enrolled = true;
            m_currentUser.fingerprint_slot = slot_id;

            getStorage().updateFingerprint(m_currentUser.rfid_uid, slot_id);
            getCloudSync().queueUpdateFp(m_currentUser.rfid_uid, slot_id);

            getDisplay().sendPrompt("Enrollment complete! Logging into Dashboard...");
            m_currentUser.is_logged_in = true;
            m_state = OrchestratorState::DASHBOARD_ACTIVE;
            vTaskDelay(pdMS_TO_TICKS(1000));
            getDisplay().sendUserData(&m_currentUser);
            break;
        }

        case EVT_FP_ENROLL_FAILED:
            getDisplay().sendPrompt("Enrollment failed. Please try again.");
            resetToStandby();
            break;

        case EVT_CMD_READ_OXIMETER:
            executeOrchestratedMeasurement();
            break;

        case EVT_ESPNOW_WEIGHT_READY: {
            float w = (evt.payload.espnow_pkt.magic == ESPNOW_MAGIC_BYTE) ?
                      evt.payload.espnow_pkt.data.weight : evt.payload.espnow_node_data.val;
            m_latestVitals.weight = w;
            ESP_LOGI(TAG, "Orchestrator Step 1 Complete: Weight = %.1f kg", w);

            // Step 2: Request height from Height Node over ESP-NOW
            getEspNow().requestHeight();
            break;
        }

        case EVT_ESPNOW_HEIGHT_READY: {
            float h_cm = (evt.payload.espnow_pkt.magic == ESPNOW_MAGIC_BYTE) ?
                         evt.payload.espnow_pkt.data.height : evt.payload.espnow_node_data.val;
            m_latestVitals.height_sonar = h_cm / 100.0f;
            m_latestVitals.height_laser = m_latestVitals.height_sonar;
            ESP_LOGI(TAG, "Orchestrator Step 2 Complete: Height = %.1f cm", h_cm);

            // Step 3: Move Stepper carriage to forehead level
            getEspNow().moveCarriage(h_cm);
            break;
        }

        case EVT_ESPNOW_STEPPER_ACK:
            ESP_LOGI(TAG, "Orchestrator Step 3 Complete: Stepper carriage aligned");
            // Step 4: Command Carriage node to measure forehead temperature
            getEspNow().requestTemperature();
            break;

        case EVT_ESPNOW_TEMP_READY: {
            float temp = (evt.payload.espnow_pkt.magic == ESPNOW_MAGIC_BYTE) ?
                         evt.payload.espnow_pkt.data.temperature : evt.payload.espnow_node_data.val;
            m_latestVitals.temperature = temp;
            ESP_LOGI(TAG, "Orchestrator Step 4 Complete: Forehead Temp = %.1f C", temp);

            // Step 5: Command Stepper to return carriage home
            getEspNow().returnCarriageHome();

            // Calculate BMI
            if (m_latestVitals.height_laser > 0.5f && m_latestVitals.weight > 10.0f) {
                m_latestVitals.bmi_laser = m_latestVitals.weight / (m_latestVitals.height_laser * m_latestVitals.height_laser);
                m_latestVitals.bmi_sonar = m_latestVitals.bmi_laser;
            }
            getDisplay().sendSensorData(&m_latestVitals);
            break;
        }

        case EVT_OXIMETER_DONE:
            m_latestVitals.heart_rate = evt.payload.oximeter.hr;
            m_latestVitals.spo2 = evt.payload.oximeter.spo2;
            if (m_latestVitals.temperature <= 0.0f) {
                m_latestVitals.temperature = evt.payload.oximeter.temperature;
            }
            if (m_latestVitals.height_laser > 0.5f && m_latestVitals.weight > 10.0f) {
                m_latestVitals.bmi_laser = m_latestVitals.weight / (m_latestVitals.height_laser * m_latestVitals.height_laser);
                m_latestVitals.bmi_sonar = m_latestVitals.bmi_laser;
            }
            getDisplay().sendSensorData(&m_latestVitals);
            getDisplay().sendPrompt("Vitals complete. Please input blood pressure on screen.");
            m_state = OrchestratorState::DASHBOARD_ACTIVE;
            break;

        case EVT_OXIMETER_NO_FINGER:
            getDisplay().sendPrompt("No finger detected on oximeter sensor. Please place finger.");
            break;

        case EVT_CMD_SAVE_READINGS:
            m_state = OrchestratorState::SAVING_RECORD;
            if (evt.payload.vitals.systolic > 0) {
                m_latestVitals.systolic = evt.payload.vitals.systolic;
                m_latestVitals.diastolic = evt.payload.vitals.diastolic;
            }
            strncpy(m_latestVitals.aha_category,
                    get_aha_bp_category(m_latestVitals.systolic, m_latestVitals.diastolic),
                    sizeof(m_latestVitals.aha_category) - 1);

            // 1. Save to local SQLite database
            getStorage().saveReading(m_latestVitals);

            // 2. Sync to Firebase
            getCloudSync().queueReading(&m_latestVitals);

            getDisplay().sendPrompt("Vitals saved to SD Card and synced to Cloud!");
            m_state = OrchestratorState::DASHBOARD_ACTIVE;
            break;

        case EVT_WIFI_CONNECTED: {
            char ip[16] = {0};
            getWifi().getIp(ip, sizeof(ip));
            getDisplay().sendStatus("WiFi Connected", true, ip, getCloudSync().isConfigured());
            getCloudSync().testConnection();
            break;
        }

        case EVT_WIFI_DISCONNECTED:
            getDisplay().sendStatus("WiFi Disconnected", false, "0.0.0.0", false);
            break;

        default:
            break;
    }
}

void KioskOrchestrator::orchestratorTask() {
    ESP_LOGI(TAG, "Kiosk Orchestrator task started on Core %d", xPortGetCoreID());
    sys_event_t evt;

    while (1) {
        if (xQueueReceive(g_sys_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            processEvent(evt);
        }
    }
}

} // namespace medicbot
