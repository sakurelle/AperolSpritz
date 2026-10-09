#include "wifi/wifi_manager.hpp"

#include "esp_eap_client.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "wifi_secrets.hpp"

#include <cstdio>
#include <cstring>

namespace {
constexpr char TAG[] = "wifi";
constexpr char AP_SSID[] = "NeedleMeter";
constexpr char AP_PASSWORD[] = "NeedleMeter2026";
constexpr EventBits_t STA_CONNECTED_BIT = BIT0;

bool equals(const char *left, const char *right) {
    return std::strcmp(left, right) == 0;
}
}

const char *wifi_mode_name(WifiMode mode) {
    switch (mode) {
    case WifiMode::Offline: return "offline";
    case WifiMode::Station: return "station";
    case WifiMode::FallbackAp: return "fallback_ap";
    }
    return "offline";
}

void WifiManager::set_status(WifiMode mode, bool connected, const char *ssid, const char *ip_address) {
    portENTER_CRITICAL(&status_lock_);
    status_ = {};
    status_.mode = mode;
    status_.connected = connected;
    std::snprintf(status_.ssid, sizeof(status_.ssid), "%s", ssid ? ssid : "");
    std::snprintf(status_.ip_address, sizeof(status_.ip_address), "%s", ip_address ? ip_address : "");
    portEXIT_CRITICAL(&status_lock_);
}

WifiStatus WifiManager::snapshot() const {
    portENTER_CRITICAL(&status_lock_);
    const WifiStatus result = status_;
    portEXIT_CRITICAL(&status_lock_);
    return result;
}

bool WifiManager::credentials_configured() const {
    if (!NEEDLE_WIFI_SECRETS_CONFIGURED || NEEDLE_WIFI_SSID[0] == '\0' || NEEDLE_WIFI_PASSWORD[0] == '\0') return false;
    if (equals(NEEDLE_WIFI_AUTH, "psk")) return true;
    return equals(NEEDLE_WIFI_AUTH, "enterprise") && NEEDLE_WIFI_USERNAME[0] != '\0';
}

esp_err_t WifiManager::start() {
    ESP_LOGI(TAG, "WiFi starting...");
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    events_ = xEventGroupCreate();
    if (!events_) return ESP_ERR_NO_MEM;
    station_netif_ = esp_netif_create_default_wifi_sta();
    if (!station_netif_) return ESP_ERR_NO_MEM;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, this, nullptr), TAG, "wifi event register");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, this, nullptr), TAG, "ip event register");

    if (!credentials_configured()) {
        ESP_LOGW(TAG, "Wi-Fi credentials are absent or incomplete; using fallback AP");
        return start_fallback_ap();
    }
    err = start_station();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "STA setup failed: %s; using fallback AP", esp_err_to_name(err));
        return start_fallback_ap();
    }
    const EventBits_t bits = xEventGroupWaitBits(events_, STA_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(12000));
    if ((bits & STA_CONNECTED_BIT) != 0) return ESP_OK;
    ESP_LOGW(TAG, "STA connection timeout; using fallback AP");
    return start_fallback_ap();
}

esp_err_t WifiManager::start_station() {
    wifi_config_t config{};
    std::snprintf(reinterpret_cast<char *>(config.sta.ssid), sizeof(config.sta.ssid), "%s", NEEDLE_WIFI_SSID);
    if (equals(NEEDLE_WIFI_AUTH, "psk")) {
        std::snprintf(reinterpret_cast<char *>(config.sta.password), sizeof(config.sta.password), "%s", NEEDLE_WIFI_PASSWORD);
        config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_LOGI(TAG, "WiFi mode: psk; SSID: %s", NEEDLE_WIFI_SSID);
    } else if (equals(NEEDLE_WIFI_AUTH, "enterprise")) {
        config.sta.threshold.authmode = WIFI_AUTH_WPA2_ENTERPRISE;
        ESP_LOGI(TAG, "WiFi mode: enterprise; SSID: %s; username configured: yes", NEEDLE_WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "Unsupported WIFI_AUTH value");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi STA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "wifi STA config");
    if (equals(NEEDLE_WIFI_AUTH, "enterprise")) {
        const char *identity = NEEDLE_WIFI_IDENTITY[0] ? NEEDLE_WIFI_IDENTITY : NEEDLE_WIFI_USERNAME;
        ESP_RETURN_ON_ERROR(esp_eap_client_set_identity(reinterpret_cast<const uint8_t *>(identity), std::strlen(identity)), TAG, "EAP identity");
        ESP_RETURN_ON_ERROR(esp_eap_client_set_username(reinterpret_cast<const uint8_t *>(NEEDLE_WIFI_USERNAME), std::strlen(NEEDLE_WIFI_USERNAME)), TAG, "EAP username");
        ESP_RETURN_ON_ERROR(esp_eap_client_set_password(reinterpret_cast<const uint8_t *>(NEEDLE_WIFI_PASSWORD), std::strlen(NEEDLE_WIFI_PASSWORD)), TAG, "EAP password");
        ESP_RETURN_ON_ERROR(esp_wifi_sta_enterprise_enable(), TAG, "EAP enable");
    }
    fallback_active_ = false;
    set_status(WifiMode::Station, false, NEEDLE_WIFI_SSID, "");
    return esp_wifi_start();
}

esp_err_t WifiManager::start_fallback_ap() {
    fallback_active_ = true;
    esp_wifi_stop();
    if (!ap_netif_) ap_netif_ = esp_netif_create_default_wifi_ap();
    if (!ap_netif_) return ESP_ERR_NO_MEM;
    wifi_config_t config{};
    std::snprintf(reinterpret_cast<char *>(config.ap.ssid), sizeof(config.ap.ssid), "%s", AP_SSID);
    std::snprintf(reinterpret_cast<char *>(config.ap.password), sizeof(config.ap.password), "%s", AP_PASSWORD);
    config.ap.ssid_len = std::strlen(AP_SSID);
    config.ap.channel = 1;
    config.ap.max_connection = 4;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "wifi AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), TAG, "wifi AP config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi AP start");
    esp_netif_ip_info_t ip{};
    ESP_RETURN_ON_ERROR(esp_netif_get_ip_info(ap_netif_, &ip), TAG, "AP IP info");
    char address[16]{};
    esp_ip4addr_ntoa(&ip.ip, address, sizeof(address));
    set_status(WifiMode::FallbackAp, true, AP_SSID, address);
    ESP_LOGI(TAG, "WiFi fallback AP started");
    ESP_LOGI(TAG, "SSID: %s", AP_SSID);
    ESP_LOGI(TAG, "IP: %s", address);
    return ESP_OK;
}

void WifiManager::event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    auto *self = static_cast<WifiManager *>(arg);
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!self->fallback_active_) {
            self->set_status(WifiMode::Station, false, NEEDLE_WIFI_SSID, "");
            ESP_LOGW(TAG, "WiFi STA disconnected; reconnecting");
            esp_wifi_connect();
        }
        return;
    }
    if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        char address[16]{};
        esp_ip4addr_ntoa(&event->ip_info.ip, address, sizeof(address));
        self->set_status(WifiMode::Station, true, NEEDLE_WIFI_SSID, address);
        xEventGroupSetBits(self->events_, STA_CONNECTED_BIT);
        ESP_LOGI(TAG, "WiFi STA connected");
        ESP_LOGI(TAG, "SSID: %s", NEEDLE_WIFI_SSID);
        ESP_LOGI(TAG, "IP: %s", address);
    }
}
