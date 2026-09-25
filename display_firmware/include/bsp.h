#ifndef BSP_H
#define BSP_H

#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"

// Backlight Pin & Level
#define BSP_LCD_BK_LIGHT_ON_LEVEL  1
#define BSP_LCD_BK_LIGHT_OFF_LEVEL (!BSP_LCD_BK_LIGHT_ON_LEVEL)
#define BSP_LCD_GPIO_BK_LIGHT      GPIO_NUM_2

// Display Control Pins
#define BSP_LCD_GPIO_HSYNC         GPIO_NUM_39
#define BSP_LCD_GPIO_VSYNC         GPIO_NUM_40
#define BSP_LCD_GPIO_DE            GPIO_NUM_41
#define BSP_LCD_GPIO_PCLK          GPIO_NUM_42
#define BSP_LCD_GPIO_DISP          GPIO_NUM_NC

// 16-bit RGB Data Pins (Sunton ESP32-8048S070C Scrambled Mapping)
#define BSP_LCD_GPIO_DATA() { \
    GPIO_NUM_15, \
    GPIO_NUM_7,  \
    GPIO_NUM_6,  \
    GPIO_NUM_5,  \
    GPIO_NUM_4,  \
    GPIO_NUM_9,  \
    GPIO_NUM_46, \
    GPIO_NUM_3,  \
    GPIO_NUM_8,  \
    GPIO_NUM_16, \
    GPIO_NUM_1,  \
    GPIO_NUM_14, \
    GPIO_NUM_21, \
    GPIO_NUM_47, \
    GPIO_NUM_48, \
    GPIO_NUM_45, \
}

// Display Resolution
#define BSP_LCD_H_RES              800
#define BSP_LCD_V_RES              480

// Display Timing Profile (16MHz Dot Clock, Sunton ESP32-8048S070 Verified Porch Settings)
#define BSP_LCD_PANEL_TIMING()         \
    (esp_lcd_rgb_timing_t)             \
    {                                  \
        .pclk_hz = 16000000,           \
        .h_res = BSP_LCD_H_RES,        \
        .v_res = BSP_LCD_V_RES,        \
        .hsync_pulse_width = 30,       \
        .hsync_back_porch = 16,        \
        .hsync_front_porch = 210,      \
        .vsync_pulse_width = 13,       \
        .vsync_back_porch = 10,        \
        .vsync_front_porch = 22,       \
        .flags.pclk_active_neg = true, \
    }

// GT911 Capacitive Touch Pins
#define BSP_TOUCH_GPIO_SCL         GPIO_NUM_20
#define BSP_TOUCH_GPIO_SDA         GPIO_NUM_19
#define BSP_TOUCH_GPIO_INT         GPIO_NUM_NC
#define BSP_TOUCH_GPIO_RST         GPIO_NUM_38

// Main Controller UART Interconnect Pins
#define DISPLAY_UART_NUM           UART_NUM_1
#define DISPLAY_UART_TX_PIN        GPIO_NUM_17
#define DISPLAY_UART_RX_PIN        GPIO_NUM_18
#define DISPLAY_UART_BAUDRATE      115200

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the 800x480 RGB LCD panel, Goodix GT911 touch, and LVGL port.
 */
esp_err_t bsp_display_init(void);

/**
 * @brief Set the backlight brightness level (0 = off, 1 = on, or PWM level).
 */
void bsp_display_backlight_set(bool enable);

#ifdef __cplusplus
}
#endif

#endif // BSP_H
