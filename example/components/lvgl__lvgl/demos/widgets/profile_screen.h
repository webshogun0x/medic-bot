#ifndef PROFILE_SCREEN_H
#define PROFILE_SCREEN_H

#include "lvgl.h"

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    char name[32];
    char gender[8];
    uint8_t age;
    float weight;
    float height;
    float bmi_data[12];
} profile_data_t;

/**********************
 *  GLOBAL FUNCTIONS
 **********************/
void profile_screen_init(lv_obj_t * parent);
void profile_update_data(const profile_data_t * data);
void profile_get_data(profile_data_t * data);

/**********************
 *  CALLBACK FUNCTIONS
 **********************/
void profile_name_edit_cb(lv_event_t * e);
void profile_gender_edit_cb(lv_event_t * e);
void profile_age_edit_cb(lv_event_t * e);
void profile_weight_edit_cb(lv_event_t * e);
void profile_height_edit_cb(lv_event_t * e);
void profile_update_btn_cb(lv_event_t * e);

#endif /* PROFILE_SCREEN_H */