#pragma once

#include "espnow_protocol.h"
#include "esp_err.h"
#include "esp_now.h"
#include "esp_idf_version.h"

namespace medicbot {

class EspNowMaster {
public:
    EspNowMaster();
    ~EspNowMaster();

    esp_err_t begin();

    esp_err_t requestWeight();
    esp_err_t requestHeight();
    esp_err_t requestGantryPosition();
    esp_err_t moveCarriage(float target_height_cm);
    esp_err_t requestTemperature();
    esp_err_t returnCarriageHome();
    esp_err_t emergencyHalt();
    esp_err_t ping(espnow_node_id_t node_id);

    esp_err_t sendCommand(espnow_node_id_t dest_node, espnow_cmd_t opcode, float value = 0.0f);

private:
    uint16_t m_seq;
    bool m_initialized;
    uint8_t m_broadcast_mac[6];

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    static void recvCallbackTrampoline(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len);
#else
    static void recvCallbackTrampoline(const uint8_t *mac_addr, const uint8_t *data, int data_len);
#endif
    void handleReceivedPacket(const uint8_t *mac_addr, const uint8_t *data, int data_len);
};

EspNowMaster &getEspNow();

} // namespace medicbot
