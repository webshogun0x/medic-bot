#include "app_config.h"
#include "system_events.h"
#include "esp_log.h"
#include "rc522.h"
#include "driver/rc522_spi.h"
#include "rc522_picc.h"
#include <cstring>
#include <cstdio>

static const char *TAG = "MFRC522_CPP";

static rc522_driver_handle_t s_driver = nullptr;
static rc522_handle_t s_scanner = nullptr;
static char s_last_uid[32] = {0};
static SemaphoreHandle_t s_uid_mutex = nullptr;

static void on_picc_state_changed(void *arg, esp_event_base_t base, int32_t event_id, void *data) {
    rc522_picc_state_changed_event_t *event = static_cast<rc522_picc_state_changed_event_t *>(data);
    rc522_picc_t *picc = event->picc;

    if (picc->state == RC522_PICC_STATE_ACTIVE) {
        char uid_str[32] = {0};
        int offset = 0;
        for (uint8_t i = 0; i < picc->uid.length && i < 10; i++) {
            offset += snprintf(uid_str + offset, sizeof(uid_str) - offset, "%02X", picc->uid.value[i]);
        }

        ESP_LOGI(TAG, "RFID Card Scanned UID: [%s]", uid_str);

        if (s_uid_mutex) {
            xSemaphoreTake(s_uid_mutex, portMAX_DELAY);
            strncpy(s_last_uid, uid_str, sizeof(s_last_uid) - 1);
            xSemaphoreGive(s_uid_mutex);
        }

        if (g_sys_event_queue) {
            sys_event_t evt;
            memset(&evt, 0, sizeof(evt));
            evt.type = EVT_RFID_SCANNED;
            strncpy(evt.payload.rfid_uid, uid_str, sizeof(evt.payload.rfid_uid) - 1);
            xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(50));
        }
    }
}

static esp_err_t try_mfrc522_init_pins(gpio_num_t mosi, gpio_num_t miso, gpio_num_t sck, gpio_num_t cs, gpio_num_t rst) {
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = mosi;
    bus_cfg.miso_io_num = miso;
    bus_cfg.sclk_io_num = sck;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;

    rc522_spi_config_t driver_config = {};
    driver_config.host_id = SPI2_HOST;
    driver_config.bus_config = &bus_cfg;
    driver_config.dev_config.spics_io_num = cs;
    driver_config.rst_io_num = rst;

    esp_err_t err = rc522_spi_create(&driver_config, &s_driver);
    if (err != ESP_OK) {
        return err;
    }

    err = rc522_driver_install(s_driver);
    if (err != ESP_OK) {
        rc522_driver_uninstall(s_driver);
        s_driver = nullptr;
        return err;
    }

    rc522_config_t scanner_config = {
        .driver = s_driver,
        .poll_interval_ms = 125,
    };

    err = rc522_create(&scanner_config, &s_scanner);
    if (err != ESP_OK) {
        rc522_driver_uninstall(s_driver);
        s_driver = nullptr;
        return err;
    }

    err = rc522_register_events(s_scanner, RC522_EVENT_PICC_STATE_CHANGED, on_picc_state_changed, nullptr);
    if (err != ESP_OK) {
        rc522_destroy(s_scanner);
        s_scanner = nullptr;
        rc522_driver_uninstall(s_driver);
        s_driver = nullptr;
        return err;
    }

    err = rc522_start(s_scanner);
    if (err != ESP_OK) {
        rc522_destroy(s_scanner);
        s_scanner = nullptr;
        rc522_driver_uninstall(s_driver);
        s_driver = nullptr;
        return err;
    }

    return ESP_OK;
}

extern "C" {

esp_err_t mfrc522_init(void) {
    if (!s_uid_mutex) {
        s_uid_mutex = xSemaphoreCreateMutex();
    }

    struct SpiTry {
        gpio_num_t mosi;
        gpio_num_t miso;
        gpio_num_t sck;
        const char *desc;
    } pin_tries[] = {
        { GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13, "MOSI=11, MISO=12, SCK=13 (Uno mapping)" },
        { GPIO_NUM_11, GPIO_NUM_13, GPIO_NUM_12, "MOSI=11, MISO=13, SCK=12 (ESP32-S3 FSPI)" },
        { GPIO_NUM_13, GPIO_NUM_12, GPIO_NUM_11, "MOSI=13, MISO=12, SCK=11" },
        { GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_11, "MOSI=12, MISO=13, SCK=11" },
        { GPIO_NUM_13, GPIO_NUM_11, GPIO_NUM_12, "MOSI=13, MISO=11, SCK=12" },
        { GPIO_NUM_12, GPIO_NUM_11, GPIO_NUM_13, "MOSI=12, MISO=11, SCK=13" },
    };

    for (size_t i = 0; i < sizeof(pin_tries) / sizeof(pin_tries[0]); i++) {
        ESP_LOGI(TAG, "Testing MFRC522 pin mapping: %s...", pin_tries[i].desc);
        esp_err_t err = try_mfrc522_init_pins(pin_tries[i].mosi, pin_tries[i].miso, pin_tries[i].sck, PIN_RC522_CS, PIN_RC522_RST);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, ">>> SUCCESS: MFRC522 RFID reader locked to %s (CS=%d, RST=%d)! <<<",
                     pin_tries[i].desc, PIN_RC522_CS, PIN_RC522_RST);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGE(TAG, "MFRC522 failed on all candidate SPI pin mappings for pins 11, 12, 13 (CS=21, RST=47)");
    return ESP_FAIL;
}

bool mfrc522_read_card(char *uid_out, size_t max_len) {
    if (!uid_out || max_len == 0 || !s_uid_mutex) return false;

    bool has_card = false;
    xSemaphoreTake(s_uid_mutex, portMAX_DELAY);
    if (strlen(s_last_uid) > 0) {
        strncpy(uid_out, s_last_uid, max_len - 1);
        uid_out[max_len - 1] = '\0';
        s_last_uid[0] = '\0';
        has_card = true;
    }
    xSemaphoreGive(s_uid_mutex);
    return has_card;
}

}
