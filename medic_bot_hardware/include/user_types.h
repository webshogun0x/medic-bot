#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char rfid[32];
    char name[64];
    char first_name[32];
    char last_name[32];
    char email[64];
    char age[16];
    char gender[16];
    char medical_id[32];
    bool fingerprint_registered;
    uint8_t fingerprint_id;
    bool is_logged_in;
} user_profile_t;

typedef struct {
    char rfid[32];
    uint32_t timestamp;
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
    bool synced_to_firebase;
} vital_readings_t;

// Measurement packet from Height & Weight module over ESP-NOW
typedef struct __attribute__((packed)) {
    float weight_kg;
    float height_sonar_cm;
    float height_lidar_cm;
    float bmi_sonar;
    float bmi_lidar;
    uint32_t timestamp;
} hw_module_packet_t;

// Health status classification helpers
static inline const char* get_hr_status(float hr) {
    if (hr <= 0) return "NIL";
    if (hr < 60.0f) return "SLOW";
    if (hr <= 100.0f) return "NORM";
    if (hr <= 160.0f) return "FAST";
    return "EXTR";
}

static inline const char* get_spo2_status(float spo2) {
    if (spo2 <= 0) return "NIL";
    if (spo2 > 95.0f) return "NORM";
    if (spo2 > 90.0f) return "MILD";
    if (spo2 > 85.0f) return "MHYP";
    return "SHYP";
}

static inline const char* get_bmi_status(float bmi) {
    if (bmi <= 0) return "NIL";
    if (bmi < 18.5f) return "UNDER";
    if (bmi < 25.0f) return "NORM";
    if (bmi < 30.0f) return "OVER";
    if (bmi < 35.0f) return "OBES1";
    if (bmi < 40.0f) return "OBES2";
    return "OBES3";
}

static inline const char* get_temp_status(float temp) {
    if (temp <= 0) return "NIL";
    if (temp < 32.0f) return "LOW";
    if (temp <= 37.5f) return "NORM";
    if (temp <= 40.0f) return "HIGH";
    return "HHYP";
}

#ifdef __cplusplus
}
#endif
