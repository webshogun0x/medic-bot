#pragma once

#include "user_types.h"
#include "esp_err.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace medicbot {

class DisplayComm {
public:
    explicit DisplayComm(uart_port_t uart_num = UART_NUM_1);
    ~DisplayComm();

    esp_err_t begin(int tx_pin, int rx_pin, uint32_t baud_rate = 115200);

    void sendPrompt(const char *message);
    void sendBootProgress(int percent, const char *task, int core_ok, int wifi_ok, int cloud_ok, int sensors_ok);
    void sendStatus(const char *message, bool wifi_ok, const char *ip_str, bool cloud_ok);
    void sendUserData(const user_profile_t *user);
    void sendSensorData(const vital_readings_t *vitals);
    void sendTyped(const char *msg_type, const char *message);
    void sendRaw(const char *format, ...);

private:
    uart_port_t m_uart_num;
    QueueHandle_t m_tx_queue;
    bool m_initialized;

    static void rxTaskTrampoline(void *arg);
    static void txTaskTrampoline(void *arg);
    void rxTask();
    void txTask();
};

DisplayComm &getDisplay();

} // namespace medicbot
