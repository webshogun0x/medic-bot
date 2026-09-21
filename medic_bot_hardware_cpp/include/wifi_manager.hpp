#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <cstddef>
#include "esp_err.h"

#ifdef __cplusplus
namespace medicbot {

class WifiManager {
public:
    WifiManager();
    ~WifiManager();

    esp_err_t begin(const char *ssid = nullptr, const char *password = nullptr);
    bool isConnected() const { return m_connected; }
    void getIp(char *buf, size_t max_len);
    esp_err_t getChannel(uint8_t *channel);

    void setConnected(bool connected) { m_connected = connected; }

private:
    bool m_connected;
    bool m_initialized;
    char m_ip_addr[16];
};

WifiManager &getWifi();

} // namespace medicbot

extern "C" {
#endif

esp_err_t wifi_manager_init(const char *ssid, const char *password);
bool wifi_manager_is_connected(void);
void wifi_manager_get_ip(char *buf, size_t max_len);
esp_err_t wifi_manager_get_channel(uint8_t *channel);

#ifdef __cplusplus
}
#endif
