#include "espnow_manager.h"
#include "app_config.h"
#include "system_events.h"
#include "wifi_manager.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ESPNOW_MGR";

static uint8_t s_hw_module_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static bool s_peer_added = false;

static void espnow_recv_callback(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (!recv_info || !data || len <= 0) return;

    ESP_LOGI(TAG, "Packet received: %d bytes from %02X:%02X:%02X:%02X:%02X:%02X",
             len,
             recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
             recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);

    if (len == sizeof(hw_module_packet_t)) {
        hw_module_packet_t pkt;
        memcpy(&pkt, data, sizeof(hw_module_packet_t));

        ESP_LOGI(TAG, "Height/Weight data: W=%.1fkg, H_Sonar=%.1fcm, H_LiDAR=%.1fcm, BMI_L=%.1f",
                 pkt.weight_kg, pkt.height_sonar_cm, pkt.height_lidar_cm, pkt.bmi_lidar);

        if (g_sys_event_queue) {
            sys_event_t evt;
            memset(&evt, 0, sizeof(evt));
            evt.type = EVT_HW_DATA_RECEIVED;
            evt.payload.hw_data = pkt;
            xQueueSendFromISR(g_sys_event_queue, &evt, NULL);
        }
    } else {
        ESP_LOGW(TAG, "Unexpected ESP-NOW packet size (%d vs %d expected)", len, (int)sizeof(hw_module_packet_t));
    }
}

static void espnow_send_callback(const uint8_t *mac_addr, esp_now_send_status_t status) {
    ESP_LOGD(TAG, "ESP-NOW TX to %02X:%02X:%02X:%02X:%02X:%02X status: %s",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
             status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL");
}

esp_err_t espnow_manager_init(void) {
    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init ESP-NOW: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_callback));
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_callback));

    // Add peer
    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, s_hw_module_mac, 6);
    
    // Sync with primary WiFi channel
    uint8_t channel = 0;
    if (wifi_manager_get_channel(&channel) == ESP_OK) {
        peer_info.channel = channel;
    } else {
        peer_info.channel = 0; // Current channel
    }
    peer_info.encrypt = false;

    err = esp_now_add_peer(&peer_info);
    if (err == ESP_OK) {
        s_peer_added = true;
        ESP_LOGI(TAG, "Added Height/Weight module peer on channel %d", peer_info.channel);
    } else {
        ESP_LOGW(TAG, "Failed to add ESP-NOW peer: %s", esp_err_to_name(err));
    }

    uint8_t my_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, my_mac);
    ESP_LOGI(TAG, "Main Controller WiFi STA MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             my_mac[0], my_mac[1], my_mac[2], my_mac[3], my_mac[4], my_mac[5]);

    return ESP_OK;
}

esp_err_t espnow_manager_request_data(void) {
    uint8_t cmd = ESPNOW_REQUEST_CMD;
    esp_err_t res = esp_now_send(s_hw_module_mac, &cmd, 1);
    if (res != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send measurement request to Height/Weight module: %s", esp_err_to_name(res));
    } else {
        ESP_LOGI(TAG, "Sent 0x%02X request to Height/Weight module", cmd);
    }
    return res;
}

esp_err_t espnow_manager_set_peer(const uint8_t *mac) {
    if (!mac) return ESP_ERR_INVALID_ARG;
    memcpy(s_hw_module_mac, mac, 6);

    if (s_peer_added) {
        esp_now_del_peer(s_hw_module_mac);
        s_peer_added = false;
    }

    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, s_hw_module_mac, 6);
    uint8_t channel = 0;
    wifi_manager_get_channel(&channel);
    peer_info.channel = channel;
    peer_info.encrypt = false;

    esp_err_t err = esp_now_add_peer(&peer_info);
    if (err == ESP_OK) {
        s_peer_added = true;
        ESP_LOGI(TAG, "Updated peer MAC to %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    return err;
}
