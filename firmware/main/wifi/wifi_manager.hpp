#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

enum class WifiMode : uint8_t { Offline, Station, FallbackAp };

struct WifiStatus {
    WifiMode mode = WifiMode::Offline;
    bool connected = false;
    char ssid[33]{};
    char ip_address[16]{};
};

const char *wifi_mode_name(WifiMode mode);

class WifiManager {
public:
    esp_err_t start();
    WifiStatus snapshot() const;

private:
    static void event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data);
    esp_err_t start_station();
    esp_err_t start_fallback_ap();
    bool credentials_configured() const;
    void set_status(WifiMode mode, bool connected, const char *ssid, const char *ip_address);

    EventGroupHandle_t events_ = nullptr;
    esp_netif_t *station_netif_ = nullptr;
    esp_netif_t *ap_netif_ = nullptr;
    bool fallback_active_ = false;
    mutable portMUX_TYPE status_lock_ = portMUX_INITIALIZER_UNLOCKED;
    WifiStatus status_{};
};
