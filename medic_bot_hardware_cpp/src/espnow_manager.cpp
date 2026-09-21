#include "espnow_manager.hpp"
#include "system_events.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "ESPNOW_MASTER_CPP";

namespace medicbot {

static EspNowMaster s_espnow_instance;

EspNowMaster &getEspNow() {
    return s_espnow_instance;
}

EspNowMaster::EspNowMaster()
    : m_seq(0), m_initialized(false) {
    memset(m_broadcast_mac, 0xFF, 6);
}

EspNowMaster::~EspNowMaster() {
    if (m_initialized) {
        esp_now_deinit();
        m_initialized = false;
    }
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void EspNowMaster::recvCallbackTrampoline(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len) {
    const uint8_t *mac_addr = recv_info ? recv_info->src_addr : nullptr;
    s_espnow_instance.handleReceivedPacket(mac_addr, data, data_len);
}
#else
void EspNowMaster::recvCallbackTrampoline(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    s_espnow_instance.handleReceivedPacket(mac_addr, data, data_len);
}
#endif

void EspNowMaster::handleReceivedPacket(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    if (data_len < (int)sizeof(espnow_kiosk_packet_t)) {
        return;
    }

    const espnow_kiosk_packet_t *pkt = reinterpret_cast<const espnow_kiosk_packet_t *>(data);

    // Validate magic byte
    if (pkt->magic != ESPNOW_MAGIC_BYTE) {
        return;
    }

    ESP_LOGI(TAG, "RX Packet: Opcode=0x%02X from Node=0x%02X (Seq=%d, Status=%d)",
             pkt->opcode, pkt->src_node, pkt->seq, pkt->status);

    // Automatic Channel Discovery / Ping Response
    if (pkt->opcode == CMD_PING) {
        espnow_cmd_t resp_op = (pkt->src_node == NODE_WEIGHT_SCALE)   ? RESP_WEIGHT :
                               (pkt->src_node == NODE_HEIGHT_SONAR)   ? RESP_HEIGHT :
                               (pkt->src_node == NODE_STEPPER_GANTRY) ? RESP_STEPPER_ACK :
                               (pkt->src_node == NODE_TEMP_CARRIAGE)  ? RESP_TEMP : RESP_STEPPER_ACK;
        ESP_LOGI(TAG, "Replying to Discovery Ping from Node 0x%02X on active Wi-Fi channel", pkt->src_node);
        sendCommand(static_cast<espnow_node_id_t>(pkt->src_node), resp_op, 0.0f);
        return;
    }

    sys_event_t evt;
    memset(&evt, 0, sizeof(evt));
    bool valid = false;

    switch (pkt->opcode) {
        case RESP_WEIGHT:
            evt.type = EVT_ESPNOW_WEIGHT_READY;
            evt.payload.espnow_pkt = *pkt;
            valid = true;
            break;

        case RESP_HEIGHT:
            evt.type = EVT_ESPNOW_HEIGHT_READY;
            evt.payload.espnow_pkt = *pkt;
            valid = true;
            break;

        case RESP_STEPPER_ACK:
            evt.type = EVT_ESPNOW_STEPPER_ACK;
            evt.payload.espnow_pkt = *pkt;
            valid = true;
            break;

        case RESP_TEMP:
            evt.type = EVT_ESPNOW_TEMP_READY;
            evt.payload.espnow_pkt = *pkt;
            valid = true;
            break;

        default:
            break;
    }

    if (valid && g_sys_event_queue) {
        xQueueSend(g_sys_event_queue, &evt, pdMS_TO_TICKS(10));
    }
}

esp_err_t EspNowMaster::begin() {
    ESP_LOGI(TAG, "Initializing ESP-NOW Master Conductor");

    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_now_register_recv_cb(recvCallbackTrampoline);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register receive callback: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register 2.4 GHz broadcast peer
    esp_now_peer_info_t peer_info;
    memset(&peer_info, 0, sizeof(peer_info));
    memcpy(peer_info.peer_addr, m_broadcast_mac, 6);
    peer_info.channel = 0;
    peer_info.encrypt = false;

    ret = esp_now_add_peer(&peer_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add broadcast peer: %s", esp_err_to_name(ret));
        return ret;
    }

    m_initialized = true;
    ESP_LOGI(TAG, "ESP-NOW Master initialized successfully");
    return ESP_OK;
}

esp_err_t EspNowMaster::sendCommand(espnow_node_id_t dest_node, espnow_cmd_t opcode, float value) {
    if (!m_initialized) return ESP_ERR_INVALID_STATE;

    espnow_kiosk_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.magic = ESPNOW_MAGIC_BYTE;
    pkt.src_node = NODE_MAIN_CONTROLLER;
    pkt.dest_node = dest_node;
    pkt.opcode = opcode;
    pkt.seq = ++m_seq;
    pkt.status = 0;

    if (opcode == CMD_MOVE_CARRIAGE) {
        pkt.data.stepper_cmd.target_height_cm = value;
        pkt.data.height = value;
    } else {
        pkt.data.height = value;
    }

    esp_err_t ret = esp_now_send(m_broadcast_mac, reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "TX Command: Opcode=0x%02X -> Node=0x%02X (Val=%.1f)", opcode, dest_node, value);
    } else {
        ESP_LOGE(TAG, "TX Failed: Opcode=0x%02X -> Node=0x%02X (%s)", opcode, dest_node, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t EspNowMaster::requestWeight() {
    return sendCommand(NODE_WEIGHT_SCALE, CMD_GET_WEIGHT);
}

esp_err_t EspNowMaster::requestHeight() {
    return sendCommand(NODE_HEIGHT_SONAR, CMD_GET_HEIGHT);
}

esp_err_t EspNowMaster::requestGantryPosition() {
    return sendCommand(NODE_HEIGHT_SONAR, CMD_GET_GANTRY_POS);
}

esp_err_t EspNowMaster::moveCarriage(float target_height_cm) {
    return sendCommand(NODE_STEPPER_GANTRY, CMD_MOVE_CARRIAGE, target_height_cm);
}

esp_err_t EspNowMaster::requestTemperature() {
    return sendCommand(NODE_TEMP_CARRIAGE, CMD_MEASURE_TEMP);
}

esp_err_t EspNowMaster::returnCarriageHome() {
    return sendCommand(NODE_STEPPER_GANTRY, CMD_RETURN_HOME);
}

esp_err_t EspNowMaster::emergencyHalt() {
    return sendCommand(NODE_STEPPER_GANTRY, CMD_EMERGENCY_HALT);
}

esp_err_t EspNowMaster::ping(espnow_node_id_t node_id) {
    return sendCommand(node_id, CMD_PING);
}

} // namespace medicbot
