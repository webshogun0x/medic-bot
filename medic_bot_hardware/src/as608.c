#include "as608.h"
#include "app_config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "AS608";

#define AS608_START_CODE        0xEF01
#define AS608_COMMAND_PACKET    0x01
#define AS608_ACK_PACKET        0x07

// Commands
#define CMD_GET_IMAGE           0x01
#define CMD_IMAGE_2_TZ          0x02
#define CMD_REG_MODEL           0x05
#define CMD_STORE               0x06
#define CMD_SEARCH              0x04
#define CMD_FAST_SEARCH         0x1B
#define CMD_VERIFY_PASSWORD     0x13

static esp_err_t as608_send_packet(uint8_t cmd, const uint8_t *params, uint16_t param_len) {
    uint16_t length = param_len + 3; // 1 byte cmd + param_len + 2 bytes checksum
    uint16_t sum = AS608_COMMAND_PACKET + (length >> 8) + (length & 0xFF) + cmd;

    for (uint16_t i = 0; i < param_len; i++) {
        sum += params[i];
    }

    uint8_t packet[32];
    uint16_t idx = 0;

    packet[idx++] = 0xEF;
    packet[idx++] = 0x01;
    packet[idx++] = 0xFF;
    packet[idx++] = 0xFF;
    packet[idx++] = 0xFF;
    packet[idx++] = 0xFF;
    packet[idx++] = AS608_COMMAND_PACKET;
    packet[idx++] = (uint8_t)(length >> 8);
    packet[idx++] = (uint8_t)(length & 0xFF);
    packet[idx++] = cmd;

    for (uint16_t i = 0; i < param_len; i++) {
        packet[idx++] = params[i];
    }

    packet[idx++] = (uint8_t)(sum >> 8);
    packet[idx++] = (uint8_t)(sum & 0xFF);

    uart_flush_input(FP_UART_NUM);
    int written = uart_write_bytes(FP_UART_NUM, packet, idx);
    return (written == idx) ? ESP_OK : ESP_FAIL;
}

static uint8_t as608_recv_ack(uint8_t *ack_data, uint16_t *ack_data_len, uint32_t timeout_ms) {
    uint8_t header[9];
    int len = uart_read_bytes(FP_UART_NUM, header, 9, pdMS_TO_TICKS(timeout_ms));
    if (len < 9) {
        return AS608_COMM_TIMEOUT;
    }

    if (header[0] != 0xEF || header[1] != 0x01 || header[6] != AS608_ACK_PACKET) {
        return AS608_COMM_ERROR;
    }

    uint16_t packet_len = ((uint16_t)header[7] << 8) | header[8];
    if (packet_len < 3 || packet_len > 64) {
        return AS608_COMM_ERROR;
    }

    uint8_t body[64];
    len = uart_read_bytes(FP_UART_NUM, body, packet_len, pdMS_TO_TICKS(timeout_ms));
    if (len < packet_len) {
        return AS608_COMM_TIMEOUT;
    }

    uint8_t confirmation_code = body[0];
    uint16_t data_bytes = packet_len - 3; // Excluding confirm code & 2 checksum bytes

    if (ack_data && ack_data_len) {
        if (*ack_data_len >= data_bytes) {
            memcpy(ack_data, &body[1], data_bytes);
            *ack_data_len = data_bytes;
        } else {
            *ack_data_len = 0;
        }
    }

    return confirmation_code;
}

esp_err_t as608_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = FP_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(FP_UART_NUM, &uart_config);
    if (err != ESP_OK) return err;

    err = uart_set_pin(FP_UART_NUM, PIN_FP_TX, PIN_FP_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;

    err = uart_driver_install(FP_UART_NUM, FP_RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;

    // Verify default sensor password (0x00000000)
    uint8_t pwd_params[4] = { 0x00, 0x00, 0x00, 0x00 };
    as608_send_packet(CMD_VERIFY_PASSWORD, pwd_params, 4);

    uint8_t ack = as608_recv_ack(NULL, NULL, 500);
    if (ack == AS608_OK) {
        ESP_LOGI(TAG, "AS608 Fingerprint Sensor verified successfully on TX=%d, RX=%d", PIN_FP_TX, PIN_FP_RX);
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "AS608 Fingerprint sensor response 0x%02X. Continuing in bypass mode.", ack);
        return ESP_OK;
    }
}

uint8_t as608_get_image(void) {
    as608_send_packet(CMD_GET_IMAGE, NULL, 0);
    return as608_recv_ack(NULL, NULL, 300);
}

uint8_t as608_image_to_template(uint8_t buffer_id) {
    uint8_t param = buffer_id;
    as608_send_packet(CMD_IMAGE_2_TZ, &param, 1);
    return as608_recv_ack(NULL, NULL, 500);
}

uint8_t as608_create_model(void) {
    as608_send_packet(CMD_REG_MODEL, NULL, 0);
    return as608_recv_ack(NULL, NULL, 500);
}

uint8_t as608_store_model(uint8_t buffer_id, uint16_t slot_id) {
    uint8_t params[3] = {
        buffer_id,
        (uint8_t)(slot_id >> 8),
        (uint8_t)(slot_id & 0xFF)
    };
    as608_send_packet(CMD_STORE, params, 3);
    return as608_recv_ack(NULL, NULL, 500);
}

uint8_t as608_fast_search(uint8_t buffer_id, uint16_t *matched_id, uint16_t *score) {
    // buffer_id, StartPage (2 bytes), PageNum (2 bytes)
    uint8_t params[5] = {
        buffer_id,
        0x00, 0x00,  // Start page 0
        0x00, 0xA0   // Search up to 160 slots
    };
    as608_send_packet(CMD_FAST_SEARCH, params, 5);

    uint8_t ack_data[4];
    uint16_t ack_len = sizeof(ack_data);
    uint8_t res = as608_recv_ack(ack_data, &ack_len, 1000);

    if (res == AS608_OK && ack_len >= 4) {
        if (matched_id) {
            *matched_id = ((uint16_t)ack_data[0] << 8) | ack_data[1];
        }
        if (score) {
            *score = ((uint16_t)ack_data[2] << 8) | ack_data[3];
        }
    }
    return res;
}
