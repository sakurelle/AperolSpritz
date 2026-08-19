#include "NetworkManager.h"

#include <WiFi.h>
#include <esp_wifi.h>

void NetworkManager::begin(const DeviceConfig &config) {
  config_ = config;
  apStarted_ = false;

  WiFi.persistent(false);

  WiFi.mode(WIFI_AP);

  esp_err_t psResult = esp_wifi_set_ps(WIFI_PS_NONE);
  if (psResult != ESP_OK) {
    Serial.printf(
        "[WIFI] WARNING: esp_wifi_set_ps failed: %d\n",
        static_cast<int>(psResult));
  }

  startAp();
}

void NetworkManager::startAp() {
  const uint64_t chipId = ESP.getEfuseMac();

  char suffix[5];
  snprintf(
      suffix,
      sizeof(suffix),
      "%04X",
      static_cast<unsigned>(chipId & 0xFFFF));

  apSsid_ = config_.apSsidPrefix + "-" + String(suffix);

  const IPAddress ip(192, 168, 4, 1);
  const IPAddress gateway(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);

  if (!WiFi.softAPConfig(ip, gateway, subnet)) {
    Serial.println("[WIFI] ERROR: softAPConfig failed");
  }

  apStarted_ = WiFi.softAP(
      apSsid_.c_str(),
      config_.apPassword.c_str(),
      1,
      false,
      4);

  if (!apStarted_) {
    Serial.println("[WIFI] ERROR: softAP start failed");
    return;
  }

  Serial.println("[WIFI] AP started");
  Serial.printf("[WIFI] SSID: %s\n", apSsid_.c_str());
  Serial.printf(
      "[WIFI] IP: %s\n",
      WiFi.softAPIP().toString().c_str());
}

void NetworkManager::update() {
  const uint32_t now = millis();

  if (now - lastHealthLogMs_ < 10000) {
    return;
  }

  lastHealthLogMs_ = now;

  wifi_mode_t mode;
  esp_wifi_get_mode(&mode);

  Serial.printf(
      "[HEALTH] uptime=%lu ms heap=%u mode=%d AP=%s clients=%u IP=%s\n",
      static_cast<unsigned long>(now),
      ESP.getFreeHeap(),
      static_cast<int>(mode),
      apStarted_ ? "ON" : "OFF",
      WiFi.softAPgetStationNum(),
      WiFi.softAPIP().toString().c_str());
}

uint8_t NetworkManager::connectedClients() const {
  return WiFi.softAPgetStationNum();
}