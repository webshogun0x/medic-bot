#pragma once

#include <cstdint>
#include <cstddef>
#include "esp_err.h"
#include "driver/uart.h"

// AS608 Protocol Status Codes
#define FINGERPRINT_OK                  0x00
#define FINGERPRINT_PACKETRECIEVEERR    0x01
#define FINGERPRINT_NOFINGER            0x02
#define FINGERPRINT_IMAGEFAIL           0x03
#define FINGERPRINT_IMAGEMESS           0x06
#define FINGERPRINT_FEATUREFAIL         0x07
#define FINGERPRINT_NOMATCH             0x08
#define FINGERPRINT_NOTFOUND            0x09
#define FINGERPRINT_ENROLLMISMATCH      0x0A
#define FINGERPRINT_BADLOCATION         0x0B
#define FINGERPRINT_DBRANGEFAIL         0x0C
#define FINGERPRINT_CLEARFAIL           0x0D
#define FINGERPRINT_PACKETRESPONSEFAIL  0x0E
#define FINGERPRINT_TIMEOUT             0xFF

namespace medicbot {

class AS608Fingerprint {
public:
    explicit AS608Fingerprint(uart_port_t uart_num = UART_NUM_2);
    ~AS608Fingerprint();

    // Hardware Initialization
    esp_err_t begin(int tx_pin, int rx_pin, uint32_t baud_rate = 57600);
    bool verifyPassword(uint32_t password = 0x00000000);

    // Core Fingerprint Operations
    uint8_t getImage();
    uint8_t image2Tz(uint8_t slot);
    uint8_t createModel();
    uint8_t storeModel(uint8_t slot, uint16_t id);
    uint8_t fingerFastSearch(uint8_t slot, uint16_t &matched_id, uint16_t &score);
    uint8_t deleteModel(uint16_t id);
    uint8_t emptyDatabase();

    bool isInitialized() const { return m_initialized; }
    uart_port_t getUartNum() const { return m_uart_num; }

private:
    uart_port_t m_uart_num;
    bool m_initialized;

    esp_err_t sendPacket(uint8_t cmd, const uint8_t *params, uint16_t param_len);
    uint8_t recvAck(uint8_t *ack_data, uint16_t *ack_data_len, uint32_t timeout_ms);
};

AS608Fingerprint &getFingerprintSensor();

} // namespace medicbot
