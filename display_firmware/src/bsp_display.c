#include "bsp.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/i2c_master.h"

static const char *TAG = "BSP_DISPLAY";

static esp_lcd_panel_handle_t s_lcd_panel = NULL;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static esp_lcd_panel_io_handle_t s_touch_io = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static lv_display_t *s_lvgl_disp = NULL;
static lv_indev_t *s_lvgl_touch_indev = NULL;

static esp_err_t app_lcd_init(esp_lcd_panel_handle_t *lp) {
    ESP_LOGI(TAG, "Initializing 800x480 RGB Parallel Panel");

    const esp_lcd_rgb_panel_config_t conf = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = BSP_LCD_PANEL_TIMING(),
        .data_width = 16,
        .num_fbs = 2,
        .hsync_gpio_num = BSP_LCD_GPIO_HSYNC,
        .vsync_gpio_num = BSP_LCD_GPIO_VSYNC,
        .de_gpio_num = BSP_LCD_GPIO_DE,
        .pclk_gpio_num = BSP_LCD_GPIO_PCLK,
        .disp_gpio_num = BSP_LCD_GPIO_DISP,
        .data_gpio_nums = BSP_LCD_GPIO_DATA(),
        .flags.fb_in_psram = 1,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&conf, lp), TAG, "RGB panel allocation failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*lp), TAG, "RGB panel init failed");

    return ESP_OK;
}

static esp_err_t app_touch_init(i2c_master_bus_handle_t *bus,
                                esp_lcd_panel_io_handle_t *tp_io,
                                esp_lcd_touch_handle_t *tp) {
    if (!*bus) {
        ESP_LOGI(TAG, "Creating I2C Master Bus for GT911 Touch (SDA=%d, SCL=%d)", BSP_TOUCH_GPIO_SDA, BSP_TOUCH_GPIO_SCL);
        const i2c_master_bus_config_t i2c_conf = {
            .i2c_port = -1,
            .sda_io_num = BSP_TOUCH_GPIO_SDA,
            .scl_io_num = BSP_TOUCH_GPIO_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = 1,
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_conf, bus), TAG, "Failed to create I2C bus");
    }

    if (!*tp_io) {
        ESP_LOGI(TAG, "Creating Touch Panel IO (GT911)");
        esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        tp_io_cfg.scl_speed_hz = 400000;
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c_v2(*bus, &tp_io_cfg, tp_io), TAG, "Failed to create touch panel IO");
    }

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = BSP_TOUCH_GPIO_RST,
        .int_gpio_num = BSP_TOUCH_GPIO_INT,
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    ESP_LOGI(TAG, "Initializing esp_lcd_touch_gt911 driver");
    return esp_lcd_touch_new_i2c_gt911(*tp_io, &tp_cfg, tp);
}

static esp_err_t app_lvgl_init(esp_lcd_panel_handle_t lp,
                               esp_lcd_touch_handle_t tp,
                               lv_display_t **lv_disp,
                               lv_indev_t **lv_touch_indev) {
    ESP_LOGI(TAG, "Initializing esp_lvgl_port on Core 1");

    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 8192,
        .task_affinity = 1,        // Pinned to Core 1
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port init failed");

    uint32_t buff_size = BSP_LCD_H_RES * 40; // 40 lines buffer in PSRAM

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = lp,
        .buffer_size = buff_size,
        .double_buffer = true,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
        }
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = false,
            .avoid_tearing = true,
        }
    };
    *lv_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = *lv_disp,
        .handle = tp,
    };
    *lv_touch_indev = lvgl_port_add_touch(&touch_cfg);

    ESP_LOGI(TAG, "LVGL Display and GT911 Touch Registered Successfully");
    return ESP_OK;
}

esp_err_t bsp_display_init(void) {
    ESP_RETURN_ON_ERROR(app_lcd_init(&s_lcd_panel), TAG, "LCD panel init error");
    ESP_RETURN_ON_ERROR(app_touch_init(&s_i2c_bus, &s_touch_io, &s_touch_handle), TAG, "Touch panel init error");
    ESP_RETURN_ON_ERROR(app_lvgl_init(s_lcd_panel, s_touch_handle, &s_lvgl_disp, &s_lvgl_touch_indev), TAG, "LVGL port error");

    // Initialize Backlight GPIO
    const gpio_config_t bk_light = {
        .pin_bit_mask = (1ULL << BSP_LCD_GPIO_BK_LIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bk_light);
    bsp_display_backlight_set(true);

    ESP_LOGI(TAG, "Sunton 7\" BSP display fully operational");
    return ESP_OK;
}

void bsp_display_backlight_set(bool enable) {
    gpio_set_level(BSP_LCD_GPIO_BK_LIGHT, enable ? BSP_LCD_BK_LIGHT_ON_LEVEL : BSP_LCD_BK_LIGHT_OFF_LEVEL);
}
