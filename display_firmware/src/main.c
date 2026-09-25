#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp.h"
#include "display_comm.h"
#include "ui_screens.h"

static const char *TAG = "MAIN_APP";

static void boot_progress_task(void *pvParameters) {
    // Initial display local hardware boot status
    vTaskDelay(pdMS_TO_TICKS(400));
    ui_boot_update_status(20, "Display HW & Touch OK. Awaiting Main Controller...", 2, 0, 0, 0);

    ESP_LOGI(TAG, "Boot progress watchdog waiting for Main Controller over UART...");

    // Periodically ping Main Controller until Main Controller sends BOOT_PROGRESS (100%) or SYSTEM_STATUS
    while (ui_get_current_screen() == UI_SCREEN_BOOT) {
        display_comm_send_cmd("{\"type\":\"DISPLAY_READY\"}");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Boot sequence synchronized with Main Controller. Screen transitioned.");
    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting MediBot Health Kiosk 7\" Display Firmware...");

    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize Sunton 7\" 800x480 RGB Panel, GT911 Touch & esp_lvgl_port (Core 1)
    ESP_LOGI(TAG, "Initializing Display Hardware & LVGL Port...");
    ESP_ERROR_CHECK(bsp_display_init());

    // 3. Initialize Inter-Controller UART Bus (TX=17, RX=18) on Core 0
    ESP_LOGI(TAG, "Initializing UART Communication Bus...");
    ESP_ERROR_CHECK(display_comm_init());

    // 4. Construct LVGL UI Screens and load Boot Screen
    ESP_LOGI(TAG, "Initializing UI Screens...");
    ui_init();

    // 5. Start Boot Progression Watchdog Task
    xTaskCreatePinnedToCore(boot_progress_task, "boot_seq", 6144, NULL, 3, NULL, 0);

    ESP_LOGI(TAG, "Display Firmware Core initialization complete");
}
