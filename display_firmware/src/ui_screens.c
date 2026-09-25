#include "ui_screens.h"
#include "display_comm.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_SCREENS";

// Screen Objects
static lv_obj_t *s_screens[10] = {0};
static ui_screen_id_t s_current_screen = UI_SCREEN_BOOT;

// Boot Screen Widgets
static lv_obj_t *s_boot_bar = NULL;
static lv_obj_t *s_boot_pct_lbl = NULL;
static lv_obj_t *s_boot_task_lbl = NULL;
static lv_obj_t *s_boot_pills[4] = {0};

// Dashboard Widgets
static lv_obj_t *s_dash_patient_name = NULL;
static lv_obj_t *s_dash_patient_id = NULL;
static lv_obj_t *s_dash_bmi_lbl = NULL;
static lv_obj_t *s_dash_bmi_arc = NULL;
static lv_obj_t *s_dash_bmi_cat_lbl = NULL;
static lv_obj_t *s_dash_hr_lbl = NULL;
static lv_obj_t *s_dash_spo2_lbl = NULL;
static lv_obj_t *s_dash_temp_lbl = NULL;
static lv_obj_t *s_dash_bp_sys_lbl = NULL;
static lv_obj_t *s_dash_bp_dia_lbl = NULL;
static lv_obj_t *s_dash_bp_badge_lbl = NULL;

// Toast Widget
static lv_obj_t *s_toast_obj = NULL;
static lv_obj_t *s_toast_lbl = NULL;
static lv_timer_t *s_toast_timer = NULL;

// Login Screen Widgets
static lv_obj_t *s_login_card_box = NULL;
static lv_obj_t *s_login_card_icon = NULL;
static lv_obj_t *s_login_card_rfid_lbl = NULL;
static lv_obj_t *s_login_card_status_lbl = NULL;
static lv_obj_t *s_login_card_result_lbl = NULL;

static lv_obj_t *s_login_fp_box = NULL;
static lv_obj_t *s_login_fp_patient_lbl = NULL;
static lv_obj_t *s_login_fp_icon = NULL;
static lv_obj_t *s_login_fp_reg_lbl = NULL;
static lv_obj_t *s_login_fp_status_lbl = NULL;
static lv_obj_t *s_login_fp_trial_pills[3] = {NULL, NULL, NULL};

// Signup Fingerprint Widgets
static lv_obj_t *s_signup_box1 = NULL;
static lv_obj_t *s_signup_lbl1 = NULL;
static lv_obj_t *s_signup_patient_lbl1 = NULL;
static lv_obj_t *s_signup_box2 = NULL;
static lv_obj_t *s_signup_lbl2 = NULL;
static lv_obj_t *s_signup_patient_lbl2 = NULL;

// Helper: AHA BP Classification
static const char *get_bp_category(int sys, int dia, lv_color_t *color) {
    if (sys >= 180 || dia >= 120) {
        if (color) *color = lv_palette_main(LV_PALETTE_RED);
        return "Crisis";
    } else if (sys >= 140 || dia >= 90) {
        if (color) *color = lv_palette_main(LV_PALETTE_ORANGE);
        return "Stage 2 HTN";
    } else if (sys >= 130 || dia >= 80) {
        if (color) *color = lv_palette_main(LV_PALETTE_YELLOW);
        return "Stage 1 HTN";
    } else if (sys >= 120 && dia < 80) {
        if (color) *color = lv_palette_main(LV_PALETTE_LIGHT_BLUE);
        return "Elevated";
    } else {
        if (color) *color = lv_palette_main(LV_PALETTE_GREEN);
        return "Optimal";
    }
}

// Forward Declarations
static void create_boot_screen(void);
static void create_idle_screen(void);
static void create_login_screens(void);
static void create_signup_screens(void);
static void create_dashboard_screen(void);
static void create_profile_screen(void);

// Navigation Event Handlers
static void on_btn_idle_existing(lv_event_t *e) {
    ui_login_card_reset();
    display_comm_send_cmd("START_LOGIN");
    ui_show_screen(UI_SCREEN_LOGIN_CARD);
}

static void on_btn_idle_new(lv_event_t *e) {
    display_comm_send_cmd("START_ENROLLMENT");
    ui_show_screen(UI_SCREEN_SIGNUP_CARD);
}

static void on_btn_cancel_back(lv_event_t *e) {
    display_comm_send_cmd("BACK");
    ui_show_screen(UI_SCREEN_IDLE);
}

void ui_login_card_reset(void) {
    lvgl_port_lock(0);
    if (s_login_card_box) {
        lv_obj_set_style_border_color(s_login_card_box, lv_color_hex(0x0284c7), 0);
    }
    if (s_login_card_icon) {
        lv_label_set_text(s_login_card_icon, LV_SYMBOL_DIRECTORY);
        lv_obj_set_style_text_color(s_login_card_icon, lv_color_hex(0x0284c7), 0);
    }
    if (s_login_card_rfid_lbl) {
        lv_label_set_text(s_login_card_rfid_lbl, "RFID Badge: Waiting for scan...");
        lv_obj_set_style_text_color(s_login_card_rfid_lbl, lv_color_hex(0x0f172a), 0);
    }
    if (s_login_card_status_lbl) {
        lv_label_set_text(s_login_card_status_lbl, "Please tap your clinic card against the RFID scanner pad");
        lv_obj_set_style_text_color(s_login_card_status_lbl, lv_color_hex(0x64748b), 0);
    }
    if (s_login_card_result_lbl) {
        lv_label_set_text(s_login_card_result_lbl, "");
    }
    lvgl_port_unlock();
}

void ui_login_card_show_scanning(const char *rfid_uid, const char *status_msg) {
    lvgl_port_lock(0);
    char buf[64];
    if (s_login_card_box) {
        lv_obj_set_style_border_color(s_login_card_box, lv_color_hex(0x0284c7), 0);
    }
    if (s_login_card_icon) {
        lv_label_set_text(s_login_card_icon, LV_SYMBOL_REFRESH);
        lv_obj_set_style_text_color(s_login_card_icon, lv_color_hex(0x0284c7), 0);
    }
    if (s_login_card_rfid_lbl) {
        snprintf(buf, sizeof(buf), "RFID Badge: %s", (rfid_uid && rfid_uid[0]) ? rfid_uid : "Scanned");
        lv_label_set_text(s_login_card_rfid_lbl, buf);
        lv_obj_set_style_text_color(s_login_card_rfid_lbl, lv_color_hex(0x0284c7), 0);
    }
    if (s_login_card_status_lbl) {
        lv_label_set_text(s_login_card_status_lbl, (status_msg && status_msg[0]) ? status_msg : "Fetching patient details from database...");
        lv_obj_set_style_text_color(s_login_card_status_lbl, lv_color_hex(0xd97706), 0);
    }
    if (s_login_card_result_lbl) {
        lv_label_set_text(s_login_card_result_lbl, "Searching database records...");
    }
    lvgl_port_unlock();
}

void ui_login_card_show_result(const char *rfid_uid, const char *name, const char *medical_id, bool is_error, const char *msg) {
    lvgl_port_lock(0);
    char buf[128];

    if (is_error) {
        if (s_login_card_box) {
            lv_obj_set_style_border_color(s_login_card_box, lv_color_hex(0xef4444), 0);
        }
        if (s_login_card_icon) {
            lv_label_set_text(s_login_card_icon, LV_SYMBOL_WARNING);
            lv_obj_set_style_text_color(s_login_card_icon, lv_color_hex(0xef4444), 0);
        }
        if (s_login_card_rfid_lbl && rfid_uid) {
            snprintf(buf, sizeof(buf), "RFID Badge: %s", rfid_uid);
            lv_label_set_text(s_login_card_rfid_lbl, buf);
            lv_obj_set_style_text_color(s_login_card_rfid_lbl, lv_color_hex(0xef4444), 0);
        }
        if (s_login_card_status_lbl) {
            lv_label_set_text(s_login_card_status_lbl, "❌ Patient Lookup Failed");
            lv_obj_set_style_text_color(s_login_card_status_lbl, lv_color_hex(0xef4444), 0);
        }
        if (s_login_card_result_lbl) {
            lv_label_set_text(s_login_card_result_lbl, (msg && msg[0]) ? msg : "Card not recognized or user not found.");
        }
    } else {
        if (s_login_card_box) {
            lv_obj_set_style_border_color(s_login_card_box, lv_color_hex(0x10b981), 0);
        }
        if (s_login_card_icon) {
            lv_label_set_text(s_login_card_icon, LV_SYMBOL_OK);
            lv_obj_set_style_text_color(s_login_card_icon, lv_color_hex(0x10b981), 0);
        }
        if (s_login_card_rfid_lbl && rfid_uid) {
            snprintf(buf, sizeof(buf), "RFID Badge: %s", rfid_uid);
            lv_label_set_text(s_login_card_rfid_lbl, buf);
            lv_obj_set_style_text_color(s_login_card_rfid_lbl, lv_color_hex(0x10b981), 0);
        }
        if (s_login_card_status_lbl) {
            lv_label_set_text(s_login_card_status_lbl, "✅ Patient Record Found!");
            lv_obj_set_style_text_color(s_login_card_status_lbl, lv_color_hex(0x10b981), 0);
        }
        if (s_login_card_result_lbl && name && medical_id) {
            snprintf(buf, sizeof(buf), "Patient Name: %s\nMedical ID: %s", name, medical_id);
            lv_label_set_text(s_login_card_result_lbl, buf);
        }
    }
    lvgl_port_unlock();
}

void ui_login_fp_show_status(const char *name, const char *medical_id, bool registered, int trial, int max_trials, const char *status_msg, bool is_error) {
    lvgl_port_lock(0);
    char buf[128];

    if (s_login_fp_patient_lbl) {
        snprintf(buf, sizeof(buf), "Patient: %s  |  Medical ID: %s",
                 (name && name[0]) ? name : "Unknown Patient",
                 (medical_id && medical_id[0]) ? medical_id : "--");
        lv_label_set_text(s_login_fp_patient_lbl, buf);
    }

    if (!registered) {
        if (s_login_fp_box) lv_obj_set_style_border_color(s_login_fp_box, lv_color_hex(0xef4444), 0);
        if (s_login_fp_icon) {
            lv_label_set_text(s_login_fp_icon, LV_SYMBOL_WARNING);
            lv_obj_set_style_text_color(s_login_fp_icon, lv_color_hex(0xef4444), 0);
        }
        if (s_login_fp_reg_lbl) {
            lv_label_set_text(s_login_fp_reg_lbl, "STATUS: FINGERPRINT NOT REGISTERED");
            lv_obj_set_style_text_color(s_login_fp_reg_lbl, lv_color_hex(0xef4444), 0);
        }
        if (s_login_fp_status_lbl) {
            lv_label_set_text(s_login_fp_status_lbl, (status_msg && status_msg[0]) ? status_msg : "No fingerprint template enrolled for this patient card.");
        }
        for (int i = 0; i < 3; i++) {
            if (s_login_fp_trial_pills[i]) {
                lv_obj_set_style_bg_color(s_login_fp_trial_pills[i], lv_color_hex(0xf1f5f9), 0);
                lv_obj_set_style_text_color(s_login_fp_trial_pills[i], lv_color_hex(0x94a3b8), 0);
            }
        }
    } else {
        if (is_error) {
            if (s_login_fp_box) lv_obj_set_style_border_color(s_login_fp_box, lv_color_hex(0xef4444), 0);
            if (s_login_fp_icon) {
                lv_label_set_text(s_login_fp_icon, LV_SYMBOL_WARNING);
                lv_obj_set_style_text_color(s_login_fp_icon, lv_color_hex(0xef4444), 0);
            }
        } else {
            if (s_login_fp_box) lv_obj_set_style_border_color(s_login_fp_box, lv_color_hex(0x10b981), 0);
            if (s_login_fp_icon) {
                lv_label_set_text(s_login_fp_icon, LV_SYMBOL_KEYBOARD);
                lv_obj_set_style_text_color(s_login_fp_icon, lv_color_hex(0x10b981), 0);
            }
        }

        if (s_login_fp_reg_lbl) {
            lv_label_set_text(s_login_fp_reg_lbl, "STATUS: BIOMETRICS REGISTERED");
            lv_obj_set_style_text_color(s_login_fp_reg_lbl, lv_color_hex(0x10b981), 0);
        }

        if (s_login_fp_status_lbl) {
            lv_label_set_text(s_login_fp_status_lbl, (status_msg && status_msg[0]) ? status_msg : "Place finger firmly on the optical scanner pad");
        }

        for (int i = 0; i < 3; i++) {
            if (!s_login_fp_trial_pills[i]) continue;
            if (i + 1 < trial) {
                // Failed previous trial
                lv_obj_set_style_bg_color(s_login_fp_trial_pills[i], lv_color_hex(0xfee2e2), 0);
                lv_obj_set_style_text_color(s_login_fp_trial_pills[i], lv_color_hex(0xef4444), 0);
            } else if (i + 1 == trial) {
                // Active current trial
                if (is_error) {
                    lv_obj_set_style_bg_color(s_login_fp_trial_pills[i], lv_color_hex(0xfef3c7), 0);
                    lv_obj_set_style_text_color(s_login_fp_trial_pills[i], lv_color_hex(0xd97706), 0);
                } else {
                    lv_obj_set_style_bg_color(s_login_fp_trial_pills[i], lv_color_hex(0xe0f2fe), 0);
                    lv_obj_set_style_text_color(s_login_fp_trial_pills[i], lv_color_hex(0x0284c7), 0);
                }
            } else {
                // Future trial
                lv_obj_set_style_bg_color(s_login_fp_trial_pills[i], lv_color_hex(0xf1f5f9), 0);
                lv_obj_set_style_text_color(s_login_fp_trial_pills[i], lv_color_hex(0x94a3b8), 0);
            }
        }
    }

    lvgl_port_unlock();
}

void ui_signup_fp_update_status(int step, const char *status_type, const char *message, const char *name, const char *medical_id) {
    lvgl_port_lock(0);
    lv_obj_t *box = (step == 1) ? s_signup_box1 : s_signup_box2;
    lv_obj_t *lbl = (step == 1) ? s_signup_lbl1 : s_signup_lbl2;
    lv_obj_t *patient_lbl = (step == 1) ? s_signup_patient_lbl1 : s_signup_patient_lbl2;

    if (patient_lbl && name && name[0]) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Patient: %s  |  Medical ID: %s", name, (medical_id && medical_id[0]) ? medical_id : "--");
        lv_label_set_text(patient_lbl, buf);
    }

    if (box && lbl && message) {
        lv_label_set_text(lbl, message);
        if (status_type && strcmp(status_type, "PROCESSING") == 0) {
            lv_obj_set_style_border_color(box, lv_color_hex(0x0284c7), 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x0284c7), 0);
        } else if (status_type && strcmp(status_type, "SUCCESS") == 0) {
            lv_obj_set_style_border_color(box, lv_color_hex(0x10b981), 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x10b981), 0);
        } else if (status_type && strcmp(status_type, "WARNING") == 0) {
            lv_obj_set_style_border_color(box, lv_color_hex(0xd97706), 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xd97706), 0);
        } else if (status_type && strcmp(status_type, "ERROR") == 0) {
            lv_obj_set_style_border_color(box, lv_color_hex(0xef4444), 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xef4444), 0);
        } else {
            lv_obj_set_style_border_color(box, lv_color_hex(0x64748b), 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x0f172a), 0);
        }
    }
    lvgl_port_unlock();
}

static void on_btn_bp_sys_adjust(lv_event_t *e) {
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    g_active_patient.systolic += delta;
    if (g_active_patient.systolic < 70) g_active_patient.systolic = 70;
    if (g_active_patient.systolic > 240) g_active_patient.systolic = 240;
    ui_dashboard_update_bp(g_active_patient.systolic, g_active_patient.diastolic);
}

static void on_btn_bp_dia_adjust(lv_event_t *e) {
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    g_active_patient.diastolic += delta;
    if (g_active_patient.diastolic < 40) g_active_patient.diastolic = 40;
    if (g_active_patient.diastolic > 150) g_active_patient.diastolic = 150;
    ui_dashboard_update_bp(g_active_patient.systolic, g_active_patient.diastolic);
}

static void on_btn_take_measurement(lv_event_t *e) {
    display_comm_send_cmd("READ_OXIMETER");
    ui_show_toast("Starting Vitals Measurement... Step on scale.");
}

static void on_btn_save_readings(lv_event_t *e) {
    display_comm_send_vitals(&g_active_patient);
    display_comm_send_cmd("SAVE_READINGS");
    ui_show_toast(LV_SYMBOL_OK " Vitals & BP Sent to Cloud Database!");
}

static void toast_timer_cb(lv_timer_t *timer) {
    if (s_toast_obj) {
        lv_obj_add_flag(s_toast_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_show_toast(const char *message) {
    if (!message || !s_toast_obj || !s_toast_lbl) return;
    lvgl_port_lock(0);
    lv_label_set_text(s_toast_lbl, message);
    lv_obj_clear_flag(s_toast_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_toast_obj);
    if (s_toast_timer) {
        lv_timer_reset(s_toast_timer);
    } else {
        s_toast_timer = lv_timer_create(toast_timer_cb, 3000, NULL);
    }
    lvgl_port_unlock();
}

void ui_show_screen(ui_screen_id_t screen_id) {
    if (screen_id >= 10 || !s_screens[screen_id]) return;
    lvgl_port_lock(0);
    s_current_screen = screen_id;
    lv_screen_load(s_screens[screen_id]);
    lvgl_port_unlock();
}

ui_screen_id_t ui_get_current_screen(void) {
    return s_current_screen;
}

void ui_boot_update_status(int percent, const char *task_text, int core_ok, int wifi_ok, int cloud_ok, int sensors_ok) {
    lvgl_port_lock(0);
    if (s_boot_bar) lv_bar_set_value(s_boot_bar, percent, LV_ANIM_ON);
    if (s_boot_pct_lbl) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", percent);
        lv_label_set_text(s_boot_pct_lbl, buf);
    }
    if (s_boot_task_lbl && task_text) {
        lv_label_set_text(s_boot_task_lbl, task_text);
    }

    // Diagnostic status pills: [0]=Core, [1]=WiFi, [2]=Cloud, [3]=Sensors
    int states[4] = {core_ok, wifi_ok, cloud_ok, sensors_ok};
    const char *text_running[4] = {"TESTING...", "CONNECTING...", "CONNECTING...", "CHECKING..."};
    const char *text_ok[4] = {"OK", "ONLINE", "CONNECTED", "READY"};

    for (int i = 0; i < 4; i++) {
        if (!s_boot_pills[i]) continue;
        if (states[i] == 2) {
            lv_label_set_text(s_boot_pills[i], text_ok[i]);
            lv_obj_set_style_text_color(s_boot_pills[i], lv_color_hex(0x10b981), 0); // Emerald Green
        } else if (states[i] == 1) {
            lv_label_set_text(s_boot_pills[i], text_running[i]);
            lv_obj_set_style_text_color(s_boot_pills[i], lv_color_hex(0xf59e0b), 0); // Amber Yellow
        } else {
            lv_label_set_text(s_boot_pills[i], "PENDING");
            lv_obj_set_style_text_color(s_boot_pills[i], lv_color_hex(0x94a3b8), 0); // Slate Gray
        }
    }

    lvgl_port_unlock();
}

void ui_dashboard_update_bp(int sys, int dia) {
    lvgl_port_lock(0);
    char buf[16];
    if (s_dash_bp_sys_lbl) {
        snprintf(buf, sizeof(buf), "%d", sys);
        lv_label_set_text(s_dash_bp_sys_lbl, buf);
    }
    if (s_dash_bp_dia_lbl) {
        snprintf(buf, sizeof(buf), "%d", dia);
        lv_label_set_text(s_dash_bp_dia_lbl, buf);
    }
    if (s_dash_bp_badge_lbl) {
        lv_color_t col;
        const char *cat = get_bp_category(sys, dia, &col);
        lv_label_set_text(s_dash_bp_badge_lbl, cat);
        lv_obj_set_style_text_color(s_dash_bp_badge_lbl, col, 0);
    }
    lvgl_port_unlock();
}

void ui_dashboard_update_vitals(const patient_record_t *p) {
    if (!p) return;
    lvgl_port_lock(0);
    char buf[64];

    if (s_dash_patient_name) lv_label_set_text(s_dash_patient_name, p->name);
    if (s_dash_patient_id) {
        snprintf(buf, sizeof(buf), "ID: %s", p->medical_id);
        lv_label_set_text(s_dash_patient_id, buf);
    }
    if (s_dash_bmi_lbl) {
        snprintf(buf, sizeof(buf), "%.1f", p->bmi);
        lv_label_set_text(s_dash_bmi_lbl, buf);
    }
    if (s_dash_bmi_arc) {
        int arc_val = (int)((p->bmi - 10.0f) / (40.0f - 10.0f) * 100.0f);
        if (arc_val < 0) arc_val = 0;
        if (arc_val > 100) arc_val = 100;
        lv_arc_set_value(s_dash_bmi_arc, arc_val);
    }
    if (s_dash_hr_lbl) {
        snprintf(buf, sizeof(buf), "%.0f", p->heart_rate);
        lv_label_set_text(s_dash_hr_lbl, buf);
    }
    if (s_dash_spo2_lbl) {
        snprintf(buf, sizeof(buf), "%.1f", p->spo2);
        lv_label_set_text(s_dash_spo2_lbl, buf);
    }
    if (s_dash_temp_lbl) {
        snprintf(buf, sizeof(buf), "%.1f", p->temperature);
        lv_label_set_text(s_dash_temp_lbl, buf);
    }

    ui_dashboard_update_bp(p->systolic, p->diastolic);
    lvgl_port_unlock();
}

// -------------------------------------------------------------
// SCREEN CREATORS (White Medical Theme)
// -------------------------------------------------------------

static void create_boot_screen(void) {
    s_screens[UI_SCREEN_BOOT] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screens[UI_SCREEN_BOOT], lv_color_hex(0xffffff), 0);

    lv_obj_t *title = lv_label_create(s_screens[UI_SCREEN_BOOT]);
    lv_label_set_text(title, "MediBot System Boot & Self-Check");
    lv_obj_set_style_text_color(title, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    s_boot_task_lbl = lv_label_create(s_screens[UI_SCREEN_BOOT]);
    lv_label_set_text(s_boot_task_lbl, "Initializing ESP32-S3 Core & Octal PSRAM...");
    lv_obj_set_style_text_color(s_boot_task_lbl, lv_color_hex(0x0284c7), 0);
    lv_obj_set_style_text_font(s_boot_task_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_boot_task_lbl, LV_ALIGN_TOP_MID, -40, 110);

    s_boot_pct_lbl = lv_label_create(s_screens[UI_SCREEN_BOOT]);
    lv_label_set_text(s_boot_pct_lbl, "0%");
    lv_obj_set_style_text_color(s_boot_pct_lbl, lv_color_hex(0x10b981), 0);
    lv_obj_set_style_text_font(s_boot_pct_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(s_boot_pct_lbl, LV_ALIGN_TOP_MID, 240, 110);

    // Progress Bar
    s_boot_bar = lv_bar_create(s_screens[UI_SCREEN_BOOT]);
    lv_obj_set_size(s_boot_bar, 540, 14);
    lv_obj_align(s_boot_bar, LV_ALIGN_TOP_MID, 0, 140);
    lv_bar_set_range(s_boot_bar, 0, 100);
    lv_bar_set_value(s_boot_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_boot_bar, lv_color_hex(0xe2e8f0), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_boot_bar, lv_color_hex(0x0284c7), LV_PART_INDICATOR);

    // Diagnostics List Container
    lv_obj_t *diag_box = lv_obj_create(s_screens[UI_SCREEN_BOOT]);
    lv_obj_set_size(diag_box, 540, 180);
    lv_obj_align(diag_box, LV_ALIGN_TOP_MID, 0, 175);
    lv_obj_set_style_bg_color(diag_box, lv_color_hex(0xf1f5f9), 0);
    lv_obj_set_style_border_color(diag_box, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_border_width(diag_box, 1, 0);
    lv_obj_set_style_radius(diag_box, 10, 0);

    const char *subsystems[] = {
        LV_SYMBOL_SETTINGS "  ESP32-S3 Dual-Core (240MHz) & PSRAM",
        LV_SYMBOL_WIFI "  Wi-Fi Station (MediBot_Clinic_Net)",
        LV_SYMBOL_UPLOAD "  Firebase Realtime Database SSL Sync",
        LV_SYMBOL_OK "  Medical Sensors (MAX30102, AS608, Scale)"
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *row = lv_obj_create(diag_box);
        lv_obj_set_size(row, 500, 32);
        lv_obj_set_pos(row, 0, i * 38);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0xe2e8f0), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, subsystems[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x1e293b), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 5, 0);

        s_boot_pills[i] = lv_label_create(row);
        lv_label_set_text(s_boot_pills[i], "PENDING");
        lv_obj_set_style_text_color(s_boot_pills[i], lv_color_hex(0x94a3b8), 0);
        lv_obj_align(s_boot_pills[i], LV_ALIGN_RIGHT_MID, -5, 0);
    }

    // Proceed Button
    lv_obj_t *btn_proceed = lv_button_create(s_screens[UI_SCREEN_BOOT]);
    lv_obj_set_size(btn_proceed, 220, 44);
    lv_obj_align(btn_proceed, LV_ALIGN_BOTTOM_RIGHT, -40, -30);
    lv_obj_set_style_bg_color(btn_proceed, lv_color_hex(0x0284c7), 0);
    lv_obj_set_style_bg_color(btn_proceed, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_proceed, (lv_event_cb_t)on_btn_cancel_back, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_lbl = lv_label_create(btn_proceed);
    lv_label_set_text(btn_lbl, LV_SYMBOL_RIGHT "  Enter Standby");
    lv_obj_center(btn_lbl);
}

static void create_idle_screen(void) {
    s_screens[UI_SCREEN_IDLE] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screens[UI_SCREEN_IDLE], lv_color_hex(0xffffff), 0);

    lv_obj_t *h1 = lv_label_create(s_screens[UI_SCREEN_IDLE]);
    lv_label_set_text(h1, "Welcome to MediBot");
    lv_obj_set_style_text_color(h1, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_text_font(h1, &lv_font_montserrat_24, 0);
    lv_obj_align(h1, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_t *sub = lv_label_create(s_screens[UI_SCREEN_IDLE]);
    lv_label_set_text(sub, "Touch an option below or tap clinic badge to begin");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 90);

    // Large Touch Card 1: Existing Patient (Grey normal, Blue pressed)
    lv_obj_t *btn_login = lv_button_create(s_screens[UI_SCREEN_IDLE]);
    lv_obj_set_size(btn_login, 340, 200);
    lv_obj_align(btn_login, LV_ALIGN_CENTER, -190, 30);
    lv_obj_set_style_bg_color(btn_login, lv_color_hex(0x64748b), 0);                // Grey normal
    lv_obj_set_style_bg_color(btn_login, lv_color_hex(0x2563eb), LV_STATE_PRESSED); // Blue on click
    lv_obj_set_style_border_color(btn_login, lv_color_hex(0x475569), 0);
    lv_obj_set_style_border_color(btn_login, lv_color_hex(0x1d4ed8), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_login, 2, 0);
    lv_obj_set_style_radius(btn_login, 16, 0);
    lv_obj_add_event_cb(btn_login, on_btn_idle_existing, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l1 = lv_label_create(btn_login);
    lv_label_set_text(l1, LV_SYMBOL_DIRECTORY "\n\nExisting Patient\n\nLogin with Badge & Biometrics");
    lv_obj_set_style_text_font(l1, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l1, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_align(l1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l1);

    // Large Touch Card 2: New Registration (Green normal, Blue pressed)
    lv_obj_t *btn_signup = lv_button_create(s_screens[UI_SCREEN_IDLE]);
    lv_obj_set_size(btn_signup, 340, 200);
    lv_obj_align(btn_signup, LV_ALIGN_CENTER, 190, 30);
    lv_obj_set_style_bg_color(btn_signup, lv_color_hex(0x10b981), 0);                // Green normal
    lv_obj_set_style_bg_color(btn_signup, lv_color_hex(0x2563eb), LV_STATE_PRESSED); // Blue on click
    lv_obj_set_style_border_color(btn_signup, lv_color_hex(0x059669), 0);
    lv_obj_set_style_border_color(btn_signup, lv_color_hex(0x1d4ed8), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_signup, 2, 0);
    lv_obj_set_style_radius(btn_signup, 16, 0);
    lv_obj_add_event_cb(btn_signup, on_btn_idle_new, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l2 = lv_label_create(btn_signup);
    lv_label_set_text(l2, LV_SYMBOL_PLUS "\n\nNew Registration\n\nEnroll & Assign Clinic Badge");
    lv_obj_set_style_text_font(l2, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l2, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_align(l2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l2);
}

static void create_dashboard_screen(void) {
    s_screens[UI_SCREEN_DASHBOARD] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screens[UI_SCREEN_DASHBOARD], lv_color_hex(0xffffff), 0);

    // Header bar
    lv_obj_t *hdr = lv_obj_create(s_screens[UI_SCREEN_DASHBOARD]);
    lv_obj_set_size(hdr, 780, 50);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0xf1f5f9), 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_color(hdr, lv_color_hex(0xcbd5e1), 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    s_dash_patient_name = lv_label_create(hdr);
    lv_label_set_text(s_dash_patient_name, LV_SYMBOL_DIRECTORY "  Sarah Jenkins");
    lv_obj_set_style_text_font(s_dash_patient_name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_dash_patient_name, lv_color_hex(0x0f172a), 0);
    lv_obj_align(s_dash_patient_name, LV_ALIGN_LEFT_MID, 10, 0);

    s_dash_patient_id = lv_label_create(hdr);
    lv_label_set_text(s_dash_patient_id, "ID: MB-94021");
    lv_obj_set_style_text_color(s_dash_patient_id, lv_color_hex(0x0284c7), 0);
    lv_obj_align(s_dash_patient_id, LV_ALIGN_RIGHT_MID, -10, 0);

    // Left Panel: Speedometer BMI Arc
    lv_obj_t *bmi_box = lv_obj_create(s_screens[UI_SCREEN_DASHBOARD]);
    lv_obj_set_size(bmi_box, 280, 320);
    lv_obj_align(bmi_box, LV_ALIGN_LEFT_MID, 10, 20);
    lv_obj_set_style_bg_color(bmi_box, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_border_color(bmi_box, lv_color_hex(0xcbd5e1), 0);
    lv_obj_clear_flag(bmi_box, LV_OBJ_FLAG_SCROLLABLE);

    s_dash_bmi_arc = lv_arc_create(bmi_box);
    lv_obj_set_size(s_dash_bmi_arc, 200, 200);
    lv_obj_align(s_dash_bmi_arc, LV_ALIGN_TOP_MID, 0, 10);
    lv_arc_set_angles(s_dash_bmi_arc, 135, 45);
    lv_arc_set_range(s_dash_bmi_arc, 0, 100);
    lv_arc_set_value(s_dash_bmi_arc, 41);
    lv_obj_set_style_arc_color(s_dash_bmi_arc, lv_color_hex(0x10b981), LV_PART_INDICATOR);

    s_dash_bmi_lbl = lv_label_create(bmi_box);
    lv_label_set_text(s_dash_bmi_lbl, "22.4");
    lv_obj_set_style_text_font(s_dash_bmi_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_dash_bmi_lbl, lv_color_hex(0x0f172a), 0);
    lv_obj_align(s_dash_bmi_lbl, LV_ALIGN_CENTER, 0, -20);

    s_dash_bmi_cat_lbl = lv_label_create(bmi_box);
    lv_label_set_text(s_dash_bmi_cat_lbl, "HEALTHY WEIGHT");
    lv_obj_set_style_text_color(s_dash_bmi_cat_lbl, lv_color_hex(0x10b981), 0);
    lv_obj_align(s_dash_bmi_cat_lbl, LV_ALIGN_BOTTOM_MID, 0, -20);

    // Right Side: Vitals Grid (HR, SpO2, Temp, and Blood Pressure with Steppers)
    // 1. Heart Rate
    lv_obj_t *card_hr = lv_obj_create(s_screens[UI_SCREEN_DASHBOARD]);
    lv_obj_set_size(card_hr, 230, 150);
    lv_obj_set_pos(card_hr, 305, 70);
    lv_obj_set_style_bg_color(card_hr, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_border_color(card_hr, lv_color_hex(0xcbd5e1), 0);

    lv_obj_t *l_hr_t = lv_label_create(card_hr);
    lv_label_set_text(l_hr_t, LV_SYMBOL_REFRESH "  Heart Rate");
    lv_obj_set_style_text_color(l_hr_t, lv_color_hex(0xe11d48), 0);

    s_dash_hr_lbl = lv_label_create(card_hr);
    lv_label_set_text(s_dash_hr_lbl, "74");
    lv_obj_set_style_text_font(s_dash_hr_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_dash_hr_lbl, lv_color_hex(0x0f172a), 0);
    lv_obj_align(s_dash_hr_lbl, LV_ALIGN_CENTER, -20, 10);

    // 2. Blood Oxygen
    lv_obj_t *card_spo2 = lv_obj_create(s_screens[UI_SCREEN_DASHBOARD]);
    lv_obj_set_size(card_spo2, 230, 150);
    lv_obj_set_pos(card_spo2, 550, 70);
    lv_obj_set_style_bg_color(card_spo2, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_border_color(card_spo2, lv_color_hex(0xcbd5e1), 0);

    lv_obj_t *l_spo2_t = lv_label_create(card_spo2);
    lv_label_set_text(l_spo2_t, LV_SYMBOL_WIFI "  Blood Oxygen");
    lv_obj_set_style_text_color(l_spo2_t, lv_color_hex(0x0284c7), 0);

    s_dash_spo2_lbl = lv_label_create(card_spo2);
    lv_label_set_text(s_dash_spo2_lbl, "98.8%");
    lv_obj_set_style_text_font(s_dash_spo2_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_dash_spo2_lbl, lv_color_hex(0x0f172a), 0);
    lv_obj_align(s_dash_spo2_lbl, LV_ALIGN_CENTER, 0, 10);

    // 3. Body Temperature
    lv_obj_t *card_temp = lv_obj_create(s_screens[UI_SCREEN_DASHBOARD]);
    lv_obj_set_size(card_temp, 230, 150);
    lv_obj_set_pos(card_temp, 305, 235);
    lv_obj_set_style_bg_color(card_temp, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_border_color(card_temp, lv_color_hex(0xcbd5e1), 0);

    lv_obj_t *l_temp_t = lv_label_create(card_temp);
    lv_label_set_text(l_temp_t, LV_SYMBOL_SETTINGS "  Body Temp");
    lv_obj_set_style_text_color(l_temp_t, lv_color_hex(0xd97706), 0);

    s_dash_temp_lbl = lv_label_create(card_temp);
    lv_label_set_text(s_dash_temp_lbl, "36.7 °C");
    lv_obj_set_style_text_font(s_dash_temp_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_dash_temp_lbl, lv_color_hex(0x0f172a), 0);
    lv_obj_align(s_dash_temp_lbl, LV_ALIGN_CENTER, 0, 10);

    // 4. Blood Pressure with Direct In-Card Steppers
    lv_obj_t *card_bp = lv_obj_create(s_screens[UI_SCREEN_DASHBOARD]);
    lv_obj_set_size(card_bp, 230, 150);
    lv_obj_set_pos(card_bp, 550, 235);
    lv_obj_set_style_bg_color(card_bp, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_border_color(card_bp, lv_color_hex(0x8b5cf6), 0);

    lv_obj_t *l_bp_t = lv_label_create(card_bp);
    lv_label_set_text(l_bp_t, "Blood Pressure (Manual)");
    lv_obj_set_style_text_color(l_bp_t, lv_color_hex(0x7c3aed), 0);
    lv_obj_align(l_bp_t, LV_ALIGN_TOP_LEFT, 0, 0);

    s_dash_bp_badge_lbl = lv_label_create(card_bp);
    lv_label_set_text(s_dash_bp_badge_lbl, "Optimal");
    lv_obj_set_style_text_color(s_dash_bp_badge_lbl, lv_color_hex(0x10b981), 0);
    lv_obj_align(s_dash_bp_badge_lbl, LV_ALIGN_TOP_RIGHT, 0, 0);

    // Systolic Stepper: [-] 120 [+]
    lv_obj_t *btn_sys_m = lv_button_create(card_bp);
    lv_obj_set_size(btn_sys_m, 26, 26);
    lv_obj_set_pos(btn_sys_m, 5, 45);
    lv_obj_set_style_bg_color(btn_sys_m, lv_color_hex(0xe2e8f0), 0);
    lv_obj_set_style_bg_color(btn_sys_m, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_sys_m, on_btn_bp_sys_adjust, LV_EVENT_CLICKED, (void *)(intptr_t)-2);
    lv_obj_t *lbl_sm = lv_label_create(btn_sys_m);
    lv_label_set_text(lbl_sm, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(lbl_sm, lv_color_hex(0x0f172a), 0);
    lv_obj_center(lbl_sm);

    s_dash_bp_sys_lbl = lv_label_create(card_bp);
    lv_label_set_text(s_dash_bp_sys_lbl, "120");
    lv_obj_set_style_text_font(s_dash_bp_sys_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_dash_bp_sys_lbl, lv_color_hex(0x0f172a), 0);
    lv_obj_set_pos(s_dash_bp_sys_lbl, 38, 48);

    lv_obj_t *btn_sys_p = lv_button_create(card_bp);
    lv_obj_set_size(btn_sys_p, 26, 26);
    lv_obj_set_pos(btn_sys_p, 75, 45);
    lv_obj_set_style_bg_color(btn_sys_p, lv_color_hex(0xe2e8f0), 0);
    lv_obj_set_style_bg_color(btn_sys_p, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_sys_p, on_btn_bp_sys_adjust, LV_EVENT_CLICKED, (void *)(intptr_t)2);
    lv_obj_t *lbl_sp = lv_label_create(btn_sys_p);
    lv_label_set_text(lbl_sp, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(lbl_sp, lv_color_hex(0x0f172a), 0);
    lv_obj_center(lbl_sp);

    // Separator "/"
    lv_obj_t *sep = lv_label_create(card_bp);
    lv_label_set_text(sep, "/");
    lv_obj_set_style_text_color(sep, lv_color_hex(0x64748b), 0);
    lv_obj_set_pos(sep, 108, 48);

    // Diastolic Stepper: [-] 80 [+]
    lv_obj_t *btn_dia_m = lv_button_create(card_bp);
    lv_obj_set_size(btn_dia_m, 26, 26);
    lv_obj_set_pos(btn_dia_m, 120, 45);
    lv_obj_set_style_bg_color(btn_dia_m, lv_color_hex(0xe2e8f0), 0);
    lv_obj_set_style_bg_color(btn_dia_m, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_dia_m, on_btn_bp_dia_adjust, LV_EVENT_CLICKED, (void *)(intptr_t)-2);
    lv_obj_t *lbl_dm = lv_label_create(btn_dia_m);
    lv_label_set_text(lbl_dm, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(lbl_dm, lv_color_hex(0x0f172a), 0);
    lv_obj_center(lbl_dm);

    s_dash_bp_dia_lbl = lv_label_create(card_bp);
    lv_label_set_text(s_dash_bp_dia_lbl, "80");
    lv_obj_set_style_text_font(s_dash_bp_dia_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_dash_bp_dia_lbl, lv_color_hex(0x0f172a), 0);
    lv_obj_set_pos(s_dash_bp_dia_lbl, 153, 48);

    lv_obj_t *btn_dia_p = lv_button_create(card_bp);
    lv_obj_set_size(btn_dia_p, 26, 26);
    lv_obj_set_pos(btn_dia_p, 185, 45);
    lv_obj_set_style_bg_color(btn_dia_p, lv_color_hex(0xe2e8f0), 0);
    lv_obj_set_style_bg_color(btn_dia_p, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_dia_p, on_btn_bp_dia_adjust, LV_EVENT_CLICKED, (void *)(intptr_t)2);
    lv_obj_t *lbl_dp = lv_label_create(btn_dia_p);
    lv_label_set_text(lbl_dp, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(lbl_dp, lv_color_hex(0x0f172a), 0);
    lv_obj_center(lbl_dp);

    lv_obj_t *bp_note = lv_label_create(card_bp);
    lv_label_set_text(bp_note, "External cuff • mmHg");
    lv_obj_set_style_text_color(bp_note, lv_color_hex(0x64748b), 0);
    lv_obj_align(bp_note, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Bottom Navigation Bar
    lv_obj_t *nav_bar = lv_obj_create(s_screens[UI_SCREEN_DASHBOARD]);
    lv_obj_set_size(nav_bar, 780, 60);
    lv_obj_align(nav_bar, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(nav_bar, lv_color_hex(0xf1f5f9), 0);
    lv_obj_set_style_border_color(nav_bar, lv_color_hex(0xcbd5e1), 0);
    lv_obj_clear_flag(nav_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_out = lv_button_create(nav_bar);
    lv_obj_set_size(btn_out, 120, 42);
    lv_obj_align(btn_out, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(btn_out, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_bg_color(btn_out, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_out, on_btn_cancel_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_out = lv_label_create(btn_out);
    lv_label_set_text(l_out, LV_SYMBOL_POWER " Logout");
    lv_obj_center(l_out);

    lv_obj_t *btn_meas = lv_button_create(nav_bar);
    lv_obj_set_size(btn_meas, 210, 42);
    lv_obj_align(btn_meas, LV_ALIGN_CENTER, -60, 0);
    lv_obj_set_style_bg_color(btn_meas, lv_color_hex(0x0284c7), 0);
    lv_obj_set_style_bg_color(btn_meas, lv_color_hex(0x1d4ed8), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_meas, on_btn_take_measurement, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_meas = lv_label_create(btn_meas);
    lv_label_set_text(l_meas, LV_SYMBOL_SETTINGS " Take Measurement");
    lv_obj_center(l_meas);

    lv_obj_t *btn_save = lv_button_create(nav_bar);
    lv_obj_set_size(btn_save, 160, 42);
    lv_obj_align(btn_save, LV_ALIGN_CENTER, 140, 0);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x10b981), 0);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_save, on_btn_save_readings, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_save = lv_label_create(btn_save);
    lv_label_set_text(l_save, LV_SYMBOL_SAVE " Save to Cloud");
    lv_obj_center(l_save);

    lv_obj_t *btn_prof = lv_button_create(nav_bar);
    lv_obj_set_size(btn_prof, 120, 42);
    lv_obj_align(btn_prof, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(btn_prof, lv_color_hex(0x475569), 0);
    lv_obj_set_style_bg_color(btn_prof, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_t *l_prof = lv_label_create(btn_prof);
    lv_label_set_text(l_prof, LV_SYMBOL_RIGHT " History");
    lv_obj_center(l_prof);
}

static void create_login_screens(void) {
    // -------------------------------------------------------------
    // Screen 1: Existing Patient RFID Card Prompt & Details Fetch
    // -------------------------------------------------------------
    s_screens[UI_SCREEN_LOGIN_CARD] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screens[UI_SCREEN_LOGIN_CARD], lv_color_hex(0xffffff), 0);

    lv_obj_t *t1 = lv_label_create(s_screens[UI_SCREEN_LOGIN_CARD]);
    lv_label_set_text(t1, "Existing Patient Verification — Step 1 of 2");
    lv_obj_set_style_text_color(t1, lv_color_hex(0x0284c7), 0);
    lv_obj_set_style_text_font(t1, &lv_font_montserrat_14, 0);
    lv_obj_align(t1, LV_ALIGN_TOP_MID, 0, 35);

    lv_obj_t *h1 = lv_label_create(s_screens[UI_SCREEN_LOGIN_CARD]);
    lv_label_set_text(h1, "Scan Your Clinic Badge");
    lv_obj_set_style_text_font(h1, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(h1, lv_color_hex(0x0f172a), 0);
    lv_obj_align(h1, LV_ALIGN_TOP_MID, 0, 65);

    s_login_card_box = lv_obj_create(s_screens[UI_SCREEN_LOGIN_CARD]);
    lv_obj_set_size(s_login_card_box, 580, 240);
    lv_obj_align(s_login_card_box, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(s_login_card_box, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_border_color(s_login_card_box, lv_color_hex(0x0284c7), 0);
    lv_obj_set_style_border_width(s_login_card_box, 2, 0);
    lv_obj_set_style_radius(s_login_card_box, 16, 0);
    lv_obj_clear_flag(s_login_card_box, LV_OBJ_FLAG_SCROLLABLE);

    s_login_card_icon = lv_label_create(s_login_card_box);
    lv_label_set_text(s_login_card_icon, LV_SYMBOL_DIRECTORY);
    lv_obj_set_style_text_color(s_login_card_icon, lv_color_hex(0x0284c7), 0);
    lv_obj_set_style_text_font(s_login_card_icon, &lv_font_montserrat_24, 0);
    lv_obj_align(s_login_card_icon, LV_ALIGN_TOP_MID, 0, 15);

    s_login_card_rfid_lbl = lv_label_create(s_login_card_box);
    lv_label_set_text(s_login_card_rfid_lbl, "RFID Badge: Waiting for scan...");
    lv_obj_set_style_text_font(s_login_card_rfid_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_login_card_rfid_lbl, lv_color_hex(0x0f172a), 0);
    lv_obj_align(s_login_card_rfid_lbl, LV_ALIGN_TOP_MID, 0, 60);

    s_login_card_status_lbl = lv_label_create(s_login_card_box);
    lv_label_set_text(s_login_card_status_lbl, "Please tap your clinic card against the RFID scanner pad");
    lv_obj_set_style_text_font(s_login_card_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_login_card_status_lbl, lv_color_hex(0x64748b), 0);
    lv_obj_align(s_login_card_status_lbl, LV_ALIGN_TOP_MID, 0, 95);

    s_login_card_result_lbl = lv_label_create(s_login_card_box);
    lv_label_set_text(s_login_card_result_lbl, "");
    lv_obj_set_style_text_font(s_login_card_result_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_login_card_result_lbl, lv_color_hex(0x10b981), 0);
    lv_obj_set_style_text_align(s_login_card_result_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_login_card_result_lbl, LV_ALIGN_TOP_MID, 0, 140);

    lv_obj_t *btn_back1 = lv_button_create(s_screens[UI_SCREEN_LOGIN_CARD]);
    lv_obj_set_size(btn_back1, 140, 42);
    lv_obj_align(btn_back1, LV_ALIGN_BOTTOM_LEFT, 40, -30);
    lv_obj_set_style_bg_color(btn_back1, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_bg_color(btn_back1, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_back1, on_btn_cancel_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_b1 = lv_label_create(btn_back1);
    lv_label_set_text(lbl_b1, LV_SYMBOL_LEFT " Cancel");
    lv_obj_center(lbl_b1);

    // -------------------------------------------------------------
    // Screen 2: Fingerprint Verification & 3-Trial Handling Screen
    // -------------------------------------------------------------
    s_screens[UI_SCREEN_LOGIN_FP] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screens[UI_SCREEN_LOGIN_FP], lv_color_hex(0xffffff), 0);

    lv_obj_t *t2 = lv_label_create(s_screens[UI_SCREEN_LOGIN_FP]);
    lv_label_set_text(t2, "Existing Patient Verification — Step 2 of 2");
    lv_obj_set_style_text_color(t2, lv_color_hex(0x0284c7), 0);
    lv_obj_set_style_text_font(t2, &lv_font_montserrat_14, 0);
    lv_obj_align(t2, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *h2 = lv_label_create(s_screens[UI_SCREEN_LOGIN_FP]);
    lv_label_set_text(h2, "Biometric Authentication");
    lv_obj_set_style_text_font(h2, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(h2, lv_color_hex(0x0f172a), 0);
    lv_obj_align(h2, LV_ALIGN_TOP_MID, 0, 55);

    s_login_fp_patient_lbl = lv_label_create(s_screens[UI_SCREEN_LOGIN_FP]);
    lv_label_set_text(s_login_fp_patient_lbl, "Patient: Waiting...  |  Medical ID: --");
    lv_obj_set_style_text_font(s_login_fp_patient_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_login_fp_patient_lbl, lv_color_hex(0x0284c7), 0);
    lv_obj_align(s_login_fp_patient_lbl, LV_ALIGN_TOP_MID, 0, 90);

    s_login_fp_box = lv_obj_create(s_screens[UI_SCREEN_LOGIN_FP]);
    lv_obj_set_size(s_login_fp_box, 600, 240);
    lv_obj_align(s_login_fp_box, LV_ALIGN_CENTER, 0, 25);
    lv_obj_set_style_bg_color(s_login_fp_box, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_border_color(s_login_fp_box, lv_color_hex(0x10b981), 0);
    lv_obj_set_style_border_width(s_login_fp_box, 2, 0);
    lv_obj_set_style_radius(s_login_fp_box, 16, 0);
    lv_obj_clear_flag(s_login_fp_box, LV_OBJ_FLAG_SCROLLABLE);

    s_login_fp_icon = lv_label_create(s_login_fp_box);
    lv_label_set_text(s_login_fp_icon, LV_SYMBOL_KEYBOARD);
    lv_obj_set_style_text_color(s_login_fp_icon, lv_color_hex(0x10b981), 0);
    lv_obj_set_style_text_font(s_login_fp_icon, &lv_font_montserrat_24, 0);
    lv_obj_align(s_login_fp_icon, LV_ALIGN_TOP_MID, 0, 15);

    s_login_fp_reg_lbl = lv_label_create(s_login_fp_box);
    lv_label_set_text(s_login_fp_reg_lbl, "STATUS: BIOMETRICS REGISTERED");
    lv_obj_set_style_text_font(s_login_fp_reg_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_login_fp_reg_lbl, lv_color_hex(0x10b981), 0);
    lv_obj_align(s_login_fp_reg_lbl, LV_ALIGN_TOP_MID, 0, 55);

    s_login_fp_status_lbl = lv_label_create(s_login_fp_box);
    lv_label_set_text(s_login_fp_status_lbl, "Place your registered finger firmly on the optical scanner pad");
    lv_obj_set_style_text_font(s_login_fp_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_login_fp_status_lbl, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_text_align(s_login_fp_status_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_login_fp_status_lbl, LV_ALIGN_TOP_MID, 0, 85);

    // Container for 3 trial pills
    lv_obj_t *trial_box = lv_obj_create(s_login_fp_box);
    lv_obj_set_size(trial_box, 480, 44);
    lv_obj_align(trial_box, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_set_style_bg_opa(trial_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(trial_box, 0, 0);
    lv_obj_clear_flag(trial_box, LV_OBJ_FLAG_SCROLLABLE);

    const char *trial_texts[3] = {
        "Trial 1 of 3",
        "Trial 2 of 3",
        "Trial 3 of 3"
    };

    for (int i = 0; i < 3; i++) {
        s_login_fp_trial_pills[i] = lv_label_create(trial_box);
        lv_label_set_text(s_login_fp_trial_pills[i], trial_texts[i]);
        lv_obj_set_size(s_login_fp_trial_pills[i], 140, 32);
        lv_obj_set_pos(s_login_fp_trial_pills[i], i * 160 + 10, 5);
        lv_obj_set_style_bg_opa(s_login_fp_trial_pills[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_login_fp_trial_pills[i], lv_color_hex(0xf1f5f9), 0);
        lv_obj_set_style_text_color(s_login_fp_trial_pills[i], lv_color_hex(0x94a3b8), 0);
        lv_obj_set_style_text_font(s_login_fp_trial_pills[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(s_login_fp_trial_pills[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_radius(s_login_fp_trial_pills[i], 8, 0);
        lv_obj_set_style_pad_top(s_login_fp_trial_pills[i], 8, 0);
    }

    lv_obj_t *btn_back2 = lv_button_create(s_screens[UI_SCREEN_LOGIN_FP]);
    lv_obj_set_size(btn_back2, 140, 42);
    lv_obj_align(btn_back2, LV_ALIGN_BOTTOM_LEFT, 40, -30);
    lv_obj_set_style_bg_color(btn_back2, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_bg_color(btn_back2, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_back2, on_btn_cancel_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_b2 = lv_label_create(btn_back2);
    lv_label_set_text(lbl_b2, LV_SYMBOL_LEFT " Cancel");
    lv_obj_center(lbl_b2);
}

static void create_signup_screens(void) {
    // -------------------------------------------------------------
    // Screen 1: Scan Clinic Card / RFID Badge
    // -------------------------------------------------------------
    s_screens[UI_SCREEN_SIGNUP_CARD] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screens[UI_SCREEN_SIGNUP_CARD], lv_color_hex(0xffffff), 0);

    lv_obj_t *t1 = lv_label_create(s_screens[UI_SCREEN_SIGNUP_CARD]);
    lv_label_set_text(t1, "Patient Registration — Step 1 of 4");
    lv_obj_set_style_text_color(t1, lv_color_hex(0x0284c7), 0);
    lv_obj_set_style_text_font(t1, &lv_font_montserrat_14, 0);
    lv_obj_align(t1, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *h1 = lv_label_create(s_screens[UI_SCREEN_SIGNUP_CARD]);
    lv_label_set_text(h1, "Tap Your Clinic Badge on Reader");
    lv_obj_set_style_text_font(h1, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(h1, lv_color_hex(0x0f172a), 0);
    lv_obj_align(h1, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *card_box1 = lv_obj_create(s_screens[UI_SCREEN_SIGNUP_CARD]);
    lv_obj_set_size(card_box1, 500, 180);
    lv_obj_align(card_box1, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(card_box1, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_border_color(card_box1, lv_color_hex(0x0284c7), 0);
    lv_obj_set_style_border_width(card_box1, 2, 0);
    lv_obj_set_style_radius(card_box1, 14, 0);

    lv_obj_t *icon_rfid = lv_label_create(card_box1);
    lv_label_set_text(icon_rfid, LV_SYMBOL_DIRECTORY "  RFID / NFC SENSOR ACTIVE\n\nPlace card firmly against the scanner pad");
    lv_obj_set_style_text_font(icon_rfid, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(icon_rfid, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(icon_rfid, lv_color_hex(0x1e293b), 0);
    lv_obj_center(icon_rfid);

    lv_obj_t *btn_back1 = lv_button_create(s_screens[UI_SCREEN_SIGNUP_CARD]);
    lv_obj_set_size(btn_back1, 140, 42);
    lv_obj_align(btn_back1, LV_ALIGN_BOTTOM_LEFT, 40, -30);
    lv_obj_set_style_bg_color(btn_back1, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_bg_color(btn_back1, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_back1, on_btn_cancel_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_b1 = lv_label_create(btn_back1);
    lv_label_set_text(lbl_b1, LV_SYMBOL_LEFT " Cancel");
    lv_obj_center(lbl_b1);

    // -------------------------------------------------------------
    // Screen 2: Patient Verification & Profile Confirmation
    // -------------------------------------------------------------
    s_screens[UI_SCREEN_SIGNUP_INFO] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screens[UI_SCREEN_SIGNUP_INFO], lv_color_hex(0xffffff), 0);

    lv_obj_t *t2 = lv_label_create(s_screens[UI_SCREEN_SIGNUP_INFO]);
    lv_label_set_text(t2, "Patient Registration — Step 2 of 4");
    lv_obj_set_style_text_color(t2, lv_color_hex(0x0284c7), 0);
    lv_obj_set_style_text_font(t2, &lv_font_montserrat_14, 0);
    lv_obj_align(t2, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *h2 = lv_label_create(s_screens[UI_SCREEN_SIGNUP_INFO]);
    lv_label_set_text(h2, "Verify Profile & Prepare Biometrics");
    lv_obj_set_style_text_font(h2, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(h2, lv_color_hex(0x0f172a), 0);
    lv_obj_align(h2, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *card_box2 = lv_obj_create(s_screens[UI_SCREEN_SIGNUP_INFO]);
    lv_obj_set_size(card_box2, 520, 200);
    lv_obj_align(card_box2, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(card_box2, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_border_color(card_box2, lv_color_hex(0x10b981), 0);
    lv_obj_set_style_border_width(card_box2, 2, 0);
    lv_obj_set_style_radius(card_box2, 14, 0);

    lv_obj_t *info_lbl = lv_label_create(card_box2);
    lv_label_set_text(info_lbl,
        LV_SYMBOL_OK " Account Verified: Patient Record Found\n\n"
        "Next, we will capture your fingerprint twice to enable\n"
        "secure, password-free login at any MediBot kiosk.");
    lv_obj_set_style_text_align(info_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(info_lbl, lv_color_hex(0x0f172a), 0);
    lv_obj_center(info_lbl);

    lv_obj_t *btn_back2 = lv_button_create(s_screens[UI_SCREEN_SIGNUP_INFO]);
    lv_obj_set_size(btn_back2, 140, 42);
    lv_obj_align(btn_back2, LV_ALIGN_BOTTOM_LEFT, 40, -30);
    lv_obj_set_style_bg_color(btn_back2, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_bg_color(btn_back2, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_back2, on_btn_cancel_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_b2 = lv_label_create(btn_back2);
    lv_label_set_text(lbl_b2, LV_SYMBOL_LEFT " Cancel");
    lv_obj_center(lbl_b2);

    // -------------------------------------------------------------
    // Screen 3: Biometric Enrollment Scan 1
    // -------------------------------------------------------------
    s_screens[UI_SCREEN_SIGNUP_FP1] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screens[UI_SCREEN_SIGNUP_FP1], lv_color_hex(0xffffff), 0);

    lv_obj_t *t3 = lv_label_create(s_screens[UI_SCREEN_SIGNUP_FP1]);
    lv_label_set_text(t3, "Biometric Enrollment — Step 3 of 4");
    lv_obj_set_style_text_color(t3, lv_color_hex(0x0284c7), 0);
    lv_obj_set_style_text_font(t3, &lv_font_montserrat_14, 0);
    lv_obj_align(t3, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *h3 = lv_label_create(s_screens[UI_SCREEN_SIGNUP_FP1]);
    lv_label_set_text(h3, "Scan 1 of 2: Place Finger on Scanner");
    lv_obj_set_style_text_font(h3, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(h3, lv_color_hex(0x0f172a), 0);
    lv_obj_align(h3, LV_ALIGN_TOP_MID, 0, 65);

    s_signup_patient_lbl1 = lv_label_create(s_screens[UI_SCREEN_SIGNUP_FP1]);
    lv_label_set_text(s_signup_patient_lbl1, "Patient: Pending  |  Medical ID: --");
    lv_obj_set_style_text_font(s_signup_patient_lbl1, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_signup_patient_lbl1, lv_color_hex(0x0284c7), 0);
    lv_obj_align(s_signup_patient_lbl1, LV_ALIGN_TOP_MID, 0, 95);

    s_signup_box1 = lv_obj_create(s_screens[UI_SCREEN_SIGNUP_FP1]);
    lv_obj_set_size(s_signup_box1, 520, 180);
    lv_obj_align(s_signup_box1, LV_ALIGN_CENTER, 0, 25);
    lv_obj_set_style_bg_color(s_signup_box1, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_border_color(s_signup_box1, lv_color_hex(0xd97706), 0);
    lv_obj_set_style_border_width(s_signup_box1, 2, 0);
    lv_obj_set_style_radius(s_signup_box1, 14, 0);

    s_signup_lbl1 = lv_label_create(s_signup_box1);
    lv_label_set_text(s_signup_lbl1, LV_SYMBOL_KEYBOARD "  OPTICAL SENSOR ACTIVE\n\nPlace your right index finger flat on the glass");
    lv_obj_set_style_text_font(s_signup_lbl1, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(s_signup_lbl1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_signup_lbl1, lv_color_hex(0xd97706), 0);
    lv_obj_center(s_signup_lbl1);

    lv_obj_t *btn_back3 = lv_button_create(s_screens[UI_SCREEN_SIGNUP_FP1]);
    lv_obj_set_size(btn_back3, 140, 42);
    lv_obj_align(btn_back3, LV_ALIGN_BOTTOM_LEFT, 40, -30);
    lv_obj_set_style_bg_color(btn_back3, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_bg_color(btn_back3, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_back3, on_btn_cancel_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_b3 = lv_label_create(btn_back3);
    lv_label_set_text(lbl_b3, LV_SYMBOL_LEFT " Cancel");
    lv_obj_center(lbl_b3);

    // -------------------------------------------------------------
    // Screen 4: Biometric Enrollment Scan 2 (Verification)
    // -------------------------------------------------------------
    s_screens[UI_SCREEN_SIGNUP_FP2] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screens[UI_SCREEN_SIGNUP_FP2], lv_color_hex(0xffffff), 0);

    lv_obj_t *t4 = lv_label_create(s_screens[UI_SCREEN_SIGNUP_FP2]);
    lv_label_set_text(t4, "Biometric Enrollment — Step 4 of 4");
    lv_obj_set_style_text_color(t4, lv_color_hex(0x0284c7), 0);
    lv_obj_set_style_text_font(t4, &lv_font_montserrat_14, 0);
    lv_obj_align(t4, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *h4 = lv_label_create(s_screens[UI_SCREEN_SIGNUP_FP2]);
    lv_label_set_text(h4, "Scan 2 of 2: Lift & Place Finger Again");
    lv_obj_set_style_text_font(h4, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(h4, lv_color_hex(0x0f172a), 0);
    lv_obj_align(h4, LV_ALIGN_TOP_MID, 0, 65);

    s_signup_patient_lbl2 = lv_label_create(s_screens[UI_SCREEN_SIGNUP_FP2]);
    lv_label_set_text(s_signup_patient_lbl2, "Patient: Pending  |  Medical ID: --");
    lv_obj_set_style_text_font(s_signup_patient_lbl2, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_signup_patient_lbl2, lv_color_hex(0x0284c7), 0);
    lv_obj_align(s_signup_patient_lbl2, LV_ALIGN_TOP_MID, 0, 95);

    s_signup_box2 = lv_obj_create(s_screens[UI_SCREEN_SIGNUP_FP2]);
    lv_obj_set_size(s_signup_box2, 520, 180);
    lv_obj_align(s_signup_box2, LV_ALIGN_CENTER, 0, 25);
    lv_obj_set_style_bg_color(s_signup_box2, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_border_color(s_signup_box2, lv_color_hex(0x10b981), 0);
    lv_obj_set_style_border_width(s_signup_box2, 2, 0);
    lv_obj_set_style_radius(s_signup_box2, 14, 0);

    s_signup_lbl2 = lv_label_create(s_signup_box2);
    lv_label_set_text(s_signup_lbl2, LV_SYMBOL_OK "  CONFIRMING BIOMETRIC TEMPLATE\n\nPlace the same finger to finalize registration");
    lv_obj_set_style_text_font(s_signup_lbl2, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(s_signup_lbl2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_signup_lbl2, lv_color_hex(0x10b981), 0);
    lv_obj_center(s_signup_lbl2);

    lv_obj_t *btn_back4 = lv_button_create(s_screens[UI_SCREEN_SIGNUP_FP2]);
    lv_obj_set_size(btn_back4, 140, 42);
    lv_obj_align(btn_back4, LV_ALIGN_BOTTOM_LEFT, 40, -30);
    lv_obj_set_style_bg_color(btn_back4, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_bg_color(btn_back4, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_back4, on_btn_cancel_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_b4 = lv_label_create(btn_back4);
    lv_label_set_text(lbl_b4, LV_SYMBOL_LEFT " Cancel");
    lv_obj_center(lbl_b4);
}

static void create_profile_screen(void) {
    s_screens[UI_SCREEN_PROFILE] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screens[UI_SCREEN_PROFILE], lv_color_hex(0xffffff), 0);

    lv_obj_t *t = lv_label_create(s_screens[UI_SCREEN_PROFILE]);
    lv_label_set_text(t, "Longitudinal BMI Trends");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x0f172a), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *btn_back = lv_button_create(s_screens[UI_SCREEN_PROFILE]);
    lv_obj_set_size(btn_back, 180, 42);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 40, -20);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x0284c7), 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x2563eb), LV_STATE_PRESSED);
    lv_obj_t *lbl_b = lv_label_create(btn_back);
    lv_label_set_text(lbl_b, LV_SYMBOL_LEFT " Vitals Dashboard");
    lv_obj_center(lbl_b);
}

void ui_init(void) {
    ESP_LOGI(TAG, "Initializing LVGL Screens & Themes (White Medical Theme)");
    lvgl_port_lock(0);

    create_boot_screen();
    create_idle_screen();
    create_login_screens();
    create_signup_screens();
    create_dashboard_screen();
    create_profile_screen();

    // Floating Global Toast Notification Box
    s_toast_obj = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_toast_obj, 440, 48);
    lv_obj_align(s_toast_obj, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(s_toast_obj, lv_color_hex(0x0f172a), 0);
    lv_obj_set_style_border_color(s_toast_obj, lv_color_hex(0x10b981), 0);
    lv_obj_set_style_border_width(s_toast_obj, 2, 0);
    lv_obj_set_style_radius(s_toast_obj, 8, 0);
    lv_obj_add_flag(s_toast_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_toast_obj, LV_OBJ_FLAG_SCROLLABLE);

    s_toast_lbl = lv_label_create(s_toast_obj);
    lv_label_set_text(s_toast_lbl, LV_SYMBOL_OK " System Initialized");
    lv_obj_set_style_text_color(s_toast_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_center(s_toast_lbl);

    // Initial Screen: Boot Screen
    lv_screen_load(s_screens[UI_SCREEN_BOOT]);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "LVGL Screens Initialized Successfully");
}
