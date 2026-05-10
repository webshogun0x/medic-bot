#include "profile_screen.h"
#include <stdio.h>
#include <string.h>

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t * profile_container;
static lv_obj_t * name_label;
static lv_obj_t * gender_label;
static lv_obj_t * age_label;
static lv_obj_t * weight_label;
static lv_obj_t * height_label;
static lv_obj_t * bmi_chart;
static lv_chart_series_t * bmi_series;
static lv_obj_t * keyboard;
static lv_obj_t * calendar;
static lv_obj_t * gender_dropdown;

static profile_data_t current_profile = {
    .name = "John Doe",
    .gender = "Male",
    .age = 35,
    .weight = 180.0f,
    .height = 5.9f,
    .bmi_data = {22.1f, 22.3f, 22.0f, 21.8f, 22.2f, 22.4f, 22.1f, 21.9f, 22.0f, 22.3f, 22.5f, 22.2f}
};

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void create_profile_fields(lv_obj_t * parent);
static void create_bmi_chart(lv_obj_t * parent);
static void show_keyboard(lv_obj_t * target);
static void show_calendar(void);
static void show_gender_dropdown(void);
static void hide_popups(void);
static void update_display(void);
static void keyboard_event_cb(lv_event_t * e);
static void calendar_event_cb(lv_event_t * e);
static void dropdown_event_cb(lv_event_t * e);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void profile_screen_init(lv_obj_t * parent)
{
    profile_container = lv_obj_create(parent);
    lv_obj_set_size(profile_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(profile_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(profile_container, 20, 0);

    create_profile_fields(profile_container);
    create_bmi_chart(profile_container);
    update_display();
}

void profile_update_data(const profile_data_t * data)
{
    if (data) {
        memcpy(&current_profile, data, sizeof(profile_data_t));
        update_display();
    }
}

void profile_get_data(profile_data_t * data)
{
    if (data) {
        memcpy(data, &current_profile, sizeof(profile_data_t));
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void create_profile_fields(lv_obj_t * parent)
{
    // Avatar placeholder
    lv_obj_t * avatar = lv_obj_create(parent);
    lv_obj_set_size(avatar, 80, 80);
    lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(avatar, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(avatar, LV_ALIGN_TOP_LEFT, 0, 0);

    // Name field
    lv_obj_t * name_cont = lv_obj_create(parent);
    lv_obj_set_size(name_cont, LV_PCT(100), 40);
    lv_obj_set_flex_flow(name_cont, LV_FLEX_FLOW_ROW);
    
    lv_obj_t * name_title = lv_label_create(name_cont);
    lv_label_set_text(name_title, "Name:");
    lv_obj_set_width(name_title, 80);
    
    name_label = lv_label_create(name_cont);
    lv_obj_set_flex_grow(name_label, 1);
    lv_obj_set_style_bg_color(name_label, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_bg_opa(name_label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(name_label, 8, 0);
    
    lv_obj_t * name_edit = lv_button_create(name_cont);
    lv_obj_set_size(name_edit, 30, 30);
    lv_obj_t * name_edit_label = lv_label_create(name_edit);
    lv_label_set_text(name_edit_label, LV_SYMBOL_EDIT);
    lv_obj_add_event_cb(name_edit, profile_name_edit_cb, LV_EVENT_CLICKED, name_label);

    // Gender field
    lv_obj_t * gender_cont = lv_obj_create(parent);
    lv_obj_set_size(gender_cont, LV_PCT(100), 40);
    lv_obj_set_flex_flow(gender_cont, LV_FLEX_FLOW_ROW);
    
    lv_obj_t * gender_title = lv_label_create(gender_cont);
    lv_label_set_text(gender_title, "Gender:");
    lv_obj_set_width(gender_title, 80);
    
    gender_label = lv_label_create(gender_cont);
    lv_obj_set_flex_grow(gender_label, 1);
    lv_obj_set_style_bg_color(gender_label, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_bg_opa(gender_label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(gender_label, 8, 0);
    
    lv_obj_t * gender_edit = lv_button_create(gender_cont);
    lv_obj_set_size(gender_edit, 30, 30);
    lv_obj_t * gender_edit_label = lv_label_create(gender_edit);
    lv_label_set_text(gender_edit_label, LV_SYMBOL_EDIT);
    lv_obj_add_event_cb(gender_edit, profile_gender_edit_cb, LV_EVENT_CLICKED, NULL);

    // Age field
    lv_obj_t * age_cont = lv_obj_create(parent);
    lv_obj_set_size(age_cont, LV_PCT(100), 40);
    lv_obj_set_flex_flow(age_cont, LV_FLEX_FLOW_ROW);
    
    lv_obj_t * age_title = lv_label_create(age_cont);
    lv_label_set_text(age_title, "Age:");
    lv_obj_set_width(age_title, 80);
    
    age_label = lv_label_create(age_cont);
    lv_obj_set_flex_grow(age_label, 1);
    lv_obj_set_style_bg_color(age_label, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_bg_opa(age_label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(age_label, 8, 0);
    
    lv_obj_t * age_edit = lv_button_create(age_cont);
    lv_obj_set_size(age_edit, 30, 30);
    lv_obj_t * age_edit_label = lv_label_create(age_edit);
    lv_label_set_text(age_edit_label, LV_SYMBOL_EDIT);
    lv_obj_add_event_cb(age_edit, profile_age_edit_cb, LV_EVENT_CLICKED, NULL);

    // Weight field
    lv_obj_t * weight_cont = lv_obj_create(parent);
    lv_obj_set_size(weight_cont, LV_PCT(100), 40);
    lv_obj_set_flex_flow(weight_cont, LV_FLEX_FLOW_ROW);
    
    lv_obj_t * weight_title = lv_label_create(weight_cont);
    lv_label_set_text(weight_title, "Weight:");
    lv_obj_set_width(weight_title, 80);
    
    weight_label = lv_label_create(weight_cont);
    lv_obj_set_flex_grow(weight_label, 1);
    lv_obj_set_style_bg_color(weight_label, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_bg_opa(weight_label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(weight_label, 8, 0);
    
    lv_obj_t * weight_edit = lv_button_create(weight_cont);
    lv_obj_set_size(weight_edit, 30, 30);
    lv_obj_t * weight_edit_label = lv_label_create(weight_edit);
    lv_label_set_text(weight_edit_label, LV_SYMBOL_EDIT);
    lv_obj_add_event_cb(weight_edit, profile_weight_edit_cb, LV_EVENT_CLICKED, weight_label);

    // Height field
    lv_obj_t * height_cont = lv_obj_create(parent);
    lv_obj_set_size(height_cont, LV_PCT(100), 40);
    lv_obj_set_flex_flow(height_cont, LV_FLEX_FLOW_ROW);
    
    lv_obj_t * height_title = lv_label_create(height_cont);
    lv_label_set_text(height_title, "Height:");
    lv_obj_set_width(height_title, 80);
    
    height_label = lv_label_create(height_cont);
    lv_obj_set_flex_grow(height_label, 1);
    lv_obj_set_style_bg_color(height_label, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_bg_opa(height_label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(height_label, 8, 0);
    
    lv_obj_t * height_edit = lv_button_create(height_cont);
    lv_obj_set_size(height_edit, 30, 30);
    lv_obj_t * height_edit_label = lv_label_create(height_edit);
    lv_label_set_text(height_edit_label, LV_SYMBOL_EDIT);
    lv_obj_add_event_cb(height_edit, profile_height_edit_cb, LV_EVENT_CLICKED, height_label);

    // Update button
    lv_obj_t * update_btn = lv_button_create(parent);
    lv_obj_set_size(update_btn, 120, 40);
    lv_obj_align(update_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_t * update_label = lv_label_create(update_btn);
    lv_label_set_text(update_label, "Update");
    lv_obj_add_event_cb(update_btn, profile_update_btn_cb, LV_EVENT_CLICKED, NULL);
}

static void create_bmi_chart(lv_obj_t * parent)
{
    lv_obj_t * chart_title = lv_label_create(parent);
    lv_label_set_text(chart_title, "BMI Records");
    lv_obj_set_style_text_font(chart_title, &lv_font_montserrat_16, 0);

    bmi_chart = lv_chart_create(parent);
    lv_obj_set_size(bmi_chart, LV_PCT(100), 200);
    lv_chart_set_type(bmi_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(bmi_chart, 12);
    lv_chart_set_range(bmi_chart, LV_CHART_AXIS_PRIMARY_Y, 18, 30);

    bmi_series = lv_chart_add_series(bmi_chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    
    // Set X-axis labels (months)
    static const char * months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    // Chart configuration complete
}

static void show_keyboard(lv_obj_t * target)
{
    hide_popups();
    keyboard = lv_keyboard_create(lv_screen_active());
    lv_obj_t * ta = lv_textarea_create(lv_screen_active());
    lv_obj_set_size(ta, LV_PCT(80), 50);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 50);
    lv_keyboard_set_textarea(keyboard, ta);
    lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_READY, target);
}

static void show_calendar(void)
{
    hide_popups();
    calendar = lv_calendar_create(lv_screen_active());
    lv_obj_set_size(calendar, 300, 300);
    lv_obj_align(calendar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(calendar, calendar_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void show_gender_dropdown(void)
{
    hide_popups();
    gender_dropdown = lv_dropdown_create(lv_screen_active());
    lv_dropdown_set_options(gender_dropdown, "Male\nFemale");
    lv_obj_align(gender_dropdown, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(gender_dropdown, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void hide_popups(void)
{
    if (keyboard) {
        lv_obj_delete(keyboard);
        keyboard = NULL;
    }
    if (calendar) {
        lv_obj_delete(calendar);
        calendar = NULL;
    }
    if (gender_dropdown) {
        lv_obj_delete(gender_dropdown);
        gender_dropdown = NULL;
    }
}

static void update_display(void)
{
    lv_label_set_text(name_label, current_profile.name);
    lv_label_set_text(gender_label, current_profile.gender);
    lv_label_set_text_fmt(age_label, "%d", current_profile.age);
    lv_label_set_text_fmt(weight_label, "%.1f lbs", current_profile.weight);
    lv_label_set_text_fmt(height_label, "%.1f ft", current_profile.height);

    // Update BMI chart
    for (int i = 0; i < 12; i++) {
        lv_chart_set_next_value(bmi_chart, bmi_series, (int32_t)(current_profile.bmi_data[i] * 10));
    }
    lv_chart_refresh(bmi_chart);
}

static void keyboard_event_cb(lv_event_t * e)
{
    lv_obj_t * target = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_t * ta = lv_keyboard_get_textarea(keyboard);
    const char * text = lv_textarea_get_text(ta);
    
    if (target == name_label) {
        strncpy(current_profile.name, text, sizeof(current_profile.name) - 1);
    } else if (target == weight_label) {
        current_profile.weight = atof(text);
    } else if (target == height_label) {
        current_profile.height = atof(text);
    }
    
    hide_popups();
    update_display();
}

static void calendar_event_cb(lv_event_t * e)
{
    lv_calendar_date_t date;
    if (lv_calendar_get_pressed_date(calendar, &date)) {
        // Calculate age from selected birth date
        current_profile.age = 2024 - date.year; // Simplified age calculation
        hide_popups();
        update_display();
    }
}

static void dropdown_event_cb(lv_event_t * e)
{
    uint16_t selected = lv_dropdown_get_selected(gender_dropdown);
    strcpy(current_profile.gender, selected == 0 ? "Male" : "Female");
    hide_popups();
    update_display();
}

/**********************
 *  CALLBACK FUNCTIONS
 **********************/
void profile_name_edit_cb(lv_event_t * e)
{
    lv_obj_t * target = (lv_obj_t *)lv_event_get_user_data(e);
    show_keyboard(target);
}

void profile_gender_edit_cb(lv_event_t * e)
{
    show_gender_dropdown();
}

void profile_age_edit_cb(lv_event_t * e)
{
    show_calendar();
}

void profile_weight_edit_cb(lv_event_t * e)
{
    lv_obj_t * target = (lv_obj_t *)lv_event_get_user_data(e);
    show_keyboard(target);
}

void profile_height_edit_cb(lv_event_t * e)
{
    lv_obj_t * target = (lv_obj_t *)lv_event_get_user_data(e);
    show_keyboard(target);
}

void profile_update_btn_cb(lv_event_t * e)
{
    // Save parameters - this can be extended to save to external storage
    update_display();
}