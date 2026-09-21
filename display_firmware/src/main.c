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
    // Initial hardware boot progression
    vTaskDelay(pdMS_TO_TICKS(400));
    ui_boot_update_status(25, "RGB DMA & GT911 Touch OK", 2, 0, 0, 0);

    vTaskDelay(pdMS_TO_TICKS(600));
    ui_boot_update_status(55, "Connecting to Main Controller...", 2, 1, 0, 0);

    // Announce display readiness to Main Controller
    display_comm_send_cmd("{\"type\":\"DISPLAY_READY\"}");

    vTaskDelay(pdMS_TO_TICKS(800));
    ui_boot_update_status(85, "Synchronizing UI Engine...", 2, 2, 1, 2);

    vTaskDelay(pdMS_TO_TICKS(700));
    ui_boot_update_status(100, "MediBot Kiosk Ready", 2, 2, 2, 2);

    vTaskDelay(pdMS_TO_TICKS(600));
    ui_show_screen(UI_SCREEN_IDLE);

    ESP_LOGI(TAG, "Boot sequence completed, Idle screen active");
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
    xTaskCreatePinnedToCore(boot_progress_task, "boot_seq", 3072, NULL, 3, NULL, 0);

    ESP_LOGI(TAG, "Display Firmware Core initialization complete");
}
