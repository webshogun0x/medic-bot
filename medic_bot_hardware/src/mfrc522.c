#include "mfrc522.h"
#include "app_config.h"
#include "system_events.h"
#include "esp_log.h"
#include "rc522.h"
#include "driver/rc522_spi.h"
#include "rc522_picc.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "MFRC522";

static rc522_driver_handle_t s_driver = NULL;
static rc522_handle_t s_scanner = NULL;
static char s_last_uid[32] = {0};
static SemaphoreHandle_t s_uid_mutex = NULL;

static void on_picc_state_changed(void *arg, esp_event_base_t base, int32_t event_id, void *data) {
    rc522_picc_state_changed_event_t *event = (rc522_picc_state_changed_event_t *)data;
    rc522_picc_t *picc = event->picc;

    if (picc->state == RC522_PICC_STATE_ACTIVE) {
        char uid_str[32] = {0};
        int offset = 0;
        for (uint8_t i = 0; i < picc->uid.length && i < 10; i++) {
            offset += snprintf(uid_str + offset, sizeof(uid_str) - offset, "%02X", picc->uid.value[i]);
        }

        ESP_LOGI(TAG, "Card Scanned UID: [%s]", uid_str);

        if (s_uid_mutex) {
            xSemaphoreTake(s_uid_mutex, portMAX_DELAY);
            strncpy(s_last_uid, uid_str, sizeof(s_last_uid) - 1);
            xSemaphoreGive(s_uid_mutex);
        }

        // Post event to system FSM queue
        if (g_sys_event_queue) {
            sys_event_t evt;
            memset(&evt, 0, sizeof(evt));
            evt.type = EVT_RFID_SCANNED;
            strncpy(evt.payload.rfid, uid_str, sizeof(evt.payload.rfid) - 1);
            xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(50));
        }
    } else if (picc->state == RC522_PICC_STATE_IDLE && event->old_state >= RC522_PICC_STATE_ACTIVE) {
        ESP_LOGD(TAG, "Card removed from field");
    }
}

esp_err_t mfrc522_init(void) {
    if (!s_uid_mutex) {
        s_uid_mutex = xSemaphoreCreateMutex();
    }

    rc522_spi_config_t driver_config = {
        .host_id = RFID_SPI_HOST,
        .bus_config = &(spi_bus_config_t){
            .miso_io_num = PIN_RFID_MISO,
            .mosi_io_num = PIN_RFID_MOSI,
            .sclk_io_num = PIN_RFID_SCK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
        },
        .dev_config = {
            .spics_io_num = PIN_RFID_CS,
        },
        .rst_io_num = PIN_RFID_RST,
    };

    esp_err_t err = rc522_spi_create(&driver_config, &s_driver);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create rc522 SPI driver: %s", esp_err_to_name(err));
        return err;
    }

    err = rc522_driver_install(s_driver);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install rc522 driver: %s", esp_err_to_name(err));
        return err;
    }

    rc522_config_t scanner_config = {
        .driver = s_driver,
        .poll_interval_ms = RFID_POLL_INTERVAL_MS,
    };

    err = rc522_create(&scanner_config, &s_scanner);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create rc522 scanner: %s", esp_err_to_name(err));
        return err;
    }

    err = rc522_register_events(s_scanner, RC522_EVENT_PICC_STATE_CHANGED, on_picc_state_changed, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register rc522 events: %s", esp_err_to_name(err));
        return err;
    }

    err = rc522_start(s_scanner);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start rc522 scanner: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "MFRC522 RFID reader started successfully with event-driven driver (MOSI:%d, MISO:%d, SCK:%d, CS:%d)",
             PIN_RFID_MOSI, PIN_RFID_MISO, PIN_RFID_SCK, PIN_RFID_CS);
    return ESP_OK;
}

bool mfrc522_read_card(char *uid_out, size_t max_len) {
    if (!uid_out || max_len == 0 || !s_uid_mutex) return false;

    bool has_card = false;
    xSemaphoreTake(s_uid_mutex, portMAX_DELAY);
    if (strlen(s_last_uid) > 0) {
        strncpy(uid_out, s_last_uid, max_len - 1);
        uid_out[max_len - 1] = '\0';
        s_last_uid[0] = '\0'; // Consume UID
        has_card = true;
    }
    xSemaphoreGive(s_uid_mutex);
    return has_card;
}

void mfrc522_halt(void) {
    // rc522 driver handles PICC halting automatically upon state transition
}
