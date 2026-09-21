#ifndef USER_TYPES_H
#define USER_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[64];
    char first_name[32];
    char last_name[32];
    char email[64];
    char phone[20];
    char rfid_uid[32];
    char age[16];
    char dob[16];
    char gender[16];
    char medical_id[32];
    bool is_logged_in;
    bool biometric_enrolled;
    int fingerprint_slot;
} user_profile_t;

typedef struct {
    float heart_rate;
    float spo2;
    float temperature;
    float weight;
    float height_laser;
    float height_sonar;
    float bmi_laser;
    float bmi_sonar;
    int systolic;
    int diastolic;
    char aha_category[24];
    uint32_t timestamp;
} vital_readings_t;

// Helper function to evaluate American Heart Association Blood Pressure Category
static inline const char* get_aha_bp_category(int sys, int dia) {
    if (sys >= 180 || dia >= 120) {
        return "Crisis";
    } else if (sys >= 140 || dia >= 90) {
        return "Stage 2 HTN";
    } else if (sys >= 130 || dia >= 80) {
        return "Stage 1 HTN";
    } else if (sys >= 120 && dia < 80) {
        return "Elevated";
    } else {
        return "Optimal";
    }
}

#ifdef __cplusplus
}
#endif

#endif // USER_TYPES_H
