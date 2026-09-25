#include "as608.hpp"
#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <cstring>

static const char *TAG = "AS608_CPP";

#define AS608_START_CODE        0xEF01
#define AS608_COMMAND_PACKET    0x01
#define AS608_ACK_PACKET        0x07

// Sensor Protocol Command Op-codes
#define CMD_GET_IMAGE           0x01
#define CMD_IMAGE_2_TZ          0x02
#define CMD_REG_MODEL           0x05
#define CMD_STORE               0x06
#define CMD_SEARCH              0x04
#define CMD_FAST_SEARCH         0x1B
#define CMD_DELETE_MODEL        0x0C
#define CMD_EMPTY_DB            0x0D
#define CMD_VERIFY_PASSWORD     0x13

namespace medicbot {

static AS608Fingerprint s_sensor_instance(FP_UART_NUM);

AS608Fingerprint &getFingerprintSensor() {
    return s_sensor_instance;
}

AS608Fingerprint::AS608Fingerprint(uart_port_t uart_num)
    : m_uart_num(uart_num), m_initialized(false) {}

AS608Fingerprint::~AS608Fingerprint() {
    if (m_initialized) {
        uart_driver_delete(m_uart_num);
        m_initialized = false;
    }
}

esp_err_t AS608Fingerprint::begin(int tx_pin, int rx_pin, uint32_t baud_rate) {
    struct AS608ConfigTry {
        int tx;
        int rx;
        uint32_t baud;
        const char *desc;
    } configs[] = {
        { tx_pin, rx_pin, 57600, "Configured (TX=17, RX=18 at 57600 baud)" },
        { rx_pin, tx_pin, 57600, "Swapped (TX=18, RX=17 at 57600 baud)" },
        { tx_pin, rx_pin, 9600, "Configured (TX=17, RX=18 at 9600 baud)" },
        { rx_pin, tx_pin, 9600, "Swapped (TX=18, RX=17 at 9600 baud)" },
        { tx_pin, rx_pin, 115200, "Configured (TX=17, RX=18 at 115200 baud)" },
        { rx_pin, tx_pin, 115200, "Swapped (TX=18, RX=17 at 115200 baud)" },
    };

    for (const auto &cfg : configs) {
        if (m_initialized) {
            uart_driver_delete(m_uart_num);
            m_initialized = false;
        }

        const uart_config_t uart_config = {
            .baud_rate = (int)cfg.baud,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 0,
            .source_clk = UART_SCLK_DEFAULT,
            .flags = 0,
        };

        if (uart_driver_install(m_uart_num, 1024, 512, 0, nullptr, 0) != ESP_OK) continue;
        if (uart_param_config(m_uart_num, &uart_config) != ESP_OK) {
            uart_driver_delete(m_uart_num);
            continue;
        }
        if (uart_set_pin(m_uart_num, cfg.tx, cfg.rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
            uart_driver_delete(m_uart_num);
            continue;
        }
        m_initialized = true;

        gpio_set_pull_mode((gpio_num_t)cfg.rx, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode((gpio_num_t)cfg.tx, GPIO_PULLUP_ONLY);

        vTaskDelay(pdMS_TO_TICKS(100));
        uart_flush(m_uart_num);

        for (int retry = 0; retry < 2; retry++) {
            if (verifyPassword()) {
                ESP_LOGI(TAG, ">>> SUCCESS: AS608 Sensor verified on TX=%d, RX=%d at %lu baud (%s)! <<<",
                         cfg.tx, cfg.rx, cfg.baud, cfg.desc);
                return ESP_OK;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    if (m_initialized) {
        uart_driver_delete(m_uart_num);
        m_initialized = false;
    }
    ESP_LOGE(TAG, "❌ AS608 Fingerprint Sensor failed password/hardware check on all pin & baud configurations!");
    return ESP_FAIL;
}

esp_err_t AS608Fingerprint::sendPacket(uint8_t cmd, const uint8_t *params, uint16_t param_len) {
    uint16_t length = param_len + 3;
    uint16_t sum = AS608_COMMAND_PACKET + (length >> 8) + (length & 0xFF) + cmd;

    for (uint16_t i = 0; i < param_len; i++) {
        sum += params[i];
    }

    uint8_t packet[64];
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

    uart_flush_input(m_uart_num);
    int written = uart_write_bytes(m_uart_num, packet, idx);
    uart_wait_tx_done(m_uart_num, pdMS_TO_TICKS(100));
    return (written == idx) ? ESP_OK : ESP_FAIL;
}

#include "esp_timer.h"

uint8_t AS608Fingerprint::recvAck(uint8_t *ack_data, uint16_t *ack_data_len, uint32_t timeout_ms) {
    int64_t start_time = esp_timer_get_time() / 1000;
    uint8_t buf[256];
    uint16_t buf_len = 0;

    while ((esp_timer_get_time() / 1000 - start_time) < timeout_ms) {
        int r = uart_read_bytes(m_uart_num, buf + buf_len, sizeof(buf) - buf_len, pdMS_TO_TICKS(20));
        if (r > 0) {
            buf_len += r;

            for (uint16_t i = 0; i + 8 < buf_len; i++) {
                if (buf[i] == 0xEF && buf[i + 1] == 0x01) {
                    uint8_t pid = buf[i + 6];
                    uint16_t packet_len = ((uint16_t)buf[i + 7] << 8) | buf[i + 8];
                    uint16_t total_expected = i + 9 + packet_len;

                    if (buf_len >= total_expected) {
                        if (pid != AS608_ACK_PACKET) {
                            ESP_LOGW(TAG, "Expected ACK PID 0x07, got 0x%02X", pid);
                            return FINGERPRINT_PACKETRECIEVEERR;
                        }

                        uint16_t calc_sum = pid + buf[i + 7] + buf[i + 8];
                        for (uint16_t k = 0; k < packet_len - 2; k++) {
                            calc_sum += buf[i + 9 + k];
                        }
                        uint16_t rx_sum = ((uint16_t)buf[i + 9 + packet_len - 2] << 8) | buf[i + 9 + packet_len - 1];

                        if (calc_sum != rx_sum) {
                            ESP_LOGW(TAG, "Checksum mismatch: calc=0x%04X, rx=0x%04X", calc_sum, rx_sum);
                            return FINGERPRINT_PACKETRECIEVEERR;
                        }

                        uint8_t confirmation_code = buf[i + 9];
                        uint16_t payload_len = packet_len - 3;

                        if (ack_data && ack_data_len) {
                            if (*ack_data_len >= payload_len) {
                                std::memcpy(ack_data, &buf[i + 10], payload_len);
                                *ack_data_len = payload_len;
                            } else {
                                *ack_data_len = 0;
                            }
                        }
                        return confirmation_code;
                    }
                }
            }
        }
    }
    return FINGERPRINT_TIMEOUT;
}

bool AS608Fingerprint::verifyPassword(uint32_t password) {
    uint8_t pwd_params[4] = {
        (uint8_t)(password >> 24),
        (uint8_t)(password >> 16),
        (uint8_t)(password >> 8),
        (uint8_t)(password & 0xFF)
    };
    sendPacket(CMD_VERIFY_PASSWORD, pwd_params, 4);
    uint8_t ack = recvAck(nullptr, nullptr, 800);
    if (ack == FINGERPRINT_OK) return true;

    sendPacket(0x17, nullptr, 0);
    ack = recvAck(nullptr, nullptr, 800);
    return (ack == FINGERPRINT_OK);
}

uint8_t AS608Fingerprint::getImage() {
    sendPacket(CMD_GET_IMAGE, nullptr, 0);
    return recvAck(nullptr, nullptr, 300);
}

uint8_t AS608Fingerprint::image2Tz(uint8_t slot) {
    uint8_t param = slot;
    sendPacket(CMD_IMAGE_2_TZ, &param, 1);
    return recvAck(nullptr, nullptr, 500);
}

uint8_t AS608Fingerprint::createModel() {
    sendPacket(CMD_REG_MODEL, nullptr, 0);
    return recvAck(nullptr, nullptr, 500);
}

uint8_t AS608Fingerprint::storeModel(uint8_t slot, uint16_t id) {
    uint8_t params[3] = {
        slot,
        (uint8_t)(id >> 8),
        (uint8_t)(id & 0xFF)
    };
    sendPacket(CMD_STORE, params, 3);
    return recvAck(nullptr, nullptr, 500);
}

uint8_t AS608Fingerprint::fingerFastSearch(uint8_t slot, uint16_t &matched_id, uint16_t &score) {
    uint8_t params[5] = {
        slot,
        0x00, 0x00,
        0x00, 0xA0
    };
    sendPacket(CMD_FAST_SEARCH, params, 5);

    uint8_t ack_data[4];
    uint16_t ack_len = sizeof(ack_data);
    uint8_t res = recvAck(ack_data, &ack_len, 1000);

    if (res == FINGERPRINT_OK && ack_len >= 4) {
        matched_id = ((uint16_t)ack_data[0] << 8) | ack_data[1];
        score = ((uint16_t)ack_data[2] << 8) | ack_data[3];
    }
    return res;
}

uint8_t AS608Fingerprint::deleteModel(uint16_t id) {
    uint8_t params[4] = {
        (uint8_t)(id >> 8),
        (uint8_t)(id & 0xFF),
        0x00, 0x01
    };
    sendPacket(CMD_DELETE_MODEL, params, 4);
    return recvAck(nullptr, nullptr, 500);
}

uint8_t AS608Fingerprint::emptyDatabase() {
    sendPacket(CMD_EMPTY_DB, nullptr, 0);
    return recvAck(nullptr, nullptr, 1000);
}

} // namespace medicbot
