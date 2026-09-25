#ifndef UI_SCREENS_H
#define UI_SCREENS_H

#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "display_comm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_SCREEN_BOOT,
    UI_SCREEN_IDLE,
    UI_SCREEN_LOGIN_CARD,
    UI_SCREEN_LOGIN_FP,
    UI_SCREEN_SIGNUP_CARD,
    UI_SCREEN_SIGNUP_INFO,
    UI_SCREEN_SIGNUP_FP1,
    UI_SCREEN_SIGNUP_FP2,
    UI_SCREEN_DASHBOARD,
    UI_SCREEN_PROFILE
} ui_screen_id_t;

/**
 * @brief Initialize all LVGL styles, screens, and start boot sequence.
 */
void ui_init(void);

/**
 * @brief Navigate to a specific screen (thread-safe, handles lvgl_port_lock).
 */
void ui_show_screen(ui_screen_id_t screen_id);

/**
 * @brief Get the currently active screen ID.
 */
ui_screen_id_t ui_get_current_screen(void);

/**
 * @brief Update Boot screen progress bar and diagnostic pills.
 * @param percent 0-100
 * @param task_text description of current initialization task
 * @param core_ok 0=pending, 1=running, 2=ok
 * @param wifi_ok 0=pending, 1=running, 2=ok
 * @param cloud_ok 0=pending, 1=running, 2=ok
 * @param sensors_ok 0=pending, 1=running, 2=ok
 */
void ui_boot_update_status(int percent, const char *task_text, int core_ok, int wifi_ok, int cloud_ok, int sensors_ok);

/**
 * @brief Update dashboard vitals and animate BMI gauge.
 */
void ui_dashboard_update_vitals(const patient_record_t *p);

/**
 * @brief Update Blood Pressure card and classification badge.
 */
void ui_dashboard_update_bp(int sys, int dia);

/**
 * @brief Show floating toast message on current screen.
 */
void ui_show_toast(const char *message);

/**
 * @brief Reset Login Card screen to initial waiting state.
 */
void ui_login_card_reset(void);

/**
 * @brief Update Login Card screen with scanned RFID UID and fetching status.
 */
void ui_login_card_show_scanning(const char *rfid_uid, const char *status_msg);

/**
 * @brief Update Login Card screen with patient details or lookup error.
 */
void ui_login_card_show_result(const char *rfid_uid, const char *name, const char *medical_id, bool is_error, const char *msg);

/**
 * @brief Update Login Fingerprint screen with patient info, registration state, and trial count.
 */
void ui_login_fp_show_status(const char *name, const char *medical_id, bool registered, int trial, int max_trials, const char *status_msg, bool is_error);

#ifdef __cplusplus
}
#endif

#endif // UI_SCREENS_H
