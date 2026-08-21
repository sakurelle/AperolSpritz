#include "wifi/wifi_manager.hpp"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_check.h"
#include <cstring>
static const char *TAG="wifi";
// Change these project constants before production deployment.
static constexpr char AP_SSID[]="NeedleMeter";
static constexpr char AP_PASSWORD[]="NeedleMeter2026";
esp_err_t WifiManager::start_access_point(){ESP_LOGI(TAG,"WiFi AP starting...");ESP_RETURN_ON_ERROR(esp_netif_init(),TAG,"netif init");ESP_RETURN_ON_ERROR(esp_event_loop_create_default(),TAG,"event loop");if(!esp_netif_create_default_wifi_ap()){ESP_LOGE(TAG,"AP netif creation failed");return ESP_ERR_NO_MEM;}wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();ESP_RETURN_ON_ERROR(esp_wifi_init(&init),TAG,"wifi init");wifi_config_t ap{};std::strncpy(reinterpret_cast<char*>(ap.ap.ssid),AP_SSID,sizeof(ap.ap.ssid));std::strncpy(reinterpret_cast<char*>(ap.ap.password),AP_PASSWORD,sizeof(ap.ap.password));ap.ap.ssid_len=std::strlen(AP_SSID);ap.ap.channel=1;ap.ap.max_connection=4;ap.ap.authmode=WIFI_AUTH_WPA2_PSK;ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP),TAG,"wifi mode");ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP,&ap),TAG,"wifi config");ESP_RETURN_ON_ERROR(esp_wifi_start(),TAG,"wifi start");ESP_LOGI(TAG,"WiFi AP started");ESP_LOGI(TAG,"SSID: %s",AP_SSID);ESP_LOGI(TAG,"IP: 192.168.4.1");return ESP_OK;}
