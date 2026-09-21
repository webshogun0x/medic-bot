#ifndef DISPLAY_COMM_H
#define DISPLAY_COMM_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Active Patient State on Display
typedef struct {
    char name[64];
    char medical_id[32];
    char age[16];
    char gender[16];
    float weight;
    float height;
    float bmi;
    float heart_rate;
    float spo2;
    float temperature;
    int systolic;
    int diastolic;
} patient_record_t;

extern patient_record_t g_active_patient;

/**
 * @brief Initialize UART driver and start background TX/RX tasks.
 */
esp_err_t display_comm_init(void);

/**
 * @brief Send a simple command string to the Main Controller.
 */
void display_comm_send_cmd(const char *cmd);

/**
 * @brief Send formatted raw string to the Main Controller.
 */
void display_comm_send_raw(const char *format, ...);

/**
 * @brief Send complete vitals packet with manual blood pressure.
 */
void display_comm_send_vitals(const patient_record_t *patient);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_COMM_H
