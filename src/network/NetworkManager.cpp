#include "NetworkManager.h"
#include <WiFi.h>

void NetworkManager::begin(const DeviceConfig &config) {
  config_ = config; apStarted_ = false; stationRequested_ = false; WiFi.persistent(false);
  if (config_.wifiSsid.length()) beginStation(); else startAp();
}
void NetworkManager::beginStation() {
  WiFi.mode(apStarted_ ? WIFI_AP_STA : WIFI_STA); WiFi.begin(config_.wifiSsid.c_str(), config_.wifiPassword.c_str());
  stationRequested_ = true; stationStartedAt_ = millis(); lastRetryAt_ = stationStartedAt_;
  Serial.printf("[WIFI] Connecting to STA SSID '%s'\n", config_.wifiSsid.c_str());
}
void NetworkManager::startAp() {
  if (apStarted_) return;
  WiFi.mode(stationRequested_ ? WIFI_AP_STA : WIFI_AP);
  const String mac = WiFi.macAddress(); const String suffix = mac.substring(max(0, static_cast<int>(mac.length()) - 5));
  apSsid_ = config_.fallbackApSsid + "-" + suffix; apSsid_.replace(":", "");
  apStarted_ = WiFi.softAP(apSsid_.c_str(), config_.fallbackApPassword.c_str());
  Serial.printf("[WIFI] Fallback AP %s: %s, IP %s\n", apStarted_ ? "started" : "FAILED", apSsid_.c_str(), WiFi.softAPIP().toString().c_str());
}
void NetworkManager::update() {
  const uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) return;
  if (stationRequested_ && !apStarted_ && now - stationStartedAt_ >= config_.wifiConnectTimeoutMs) startAp();
  if (config_.wifiSsid.length() && now - lastRetryAt_ >= config_.wifiRetryIntervalMs) beginStation();
}
void NetworkManager::requestReconnect(const DeviceConfig &config) { config_ = config; WiFi.disconnect(false, false); if (config_.wifiSsid.length()) beginStation(); else startAp(); }
String NetworkManager::modeName() const { if (WiFi.status() == WL_CONNECTED) return apStarted_ ? "STA+AP" : "STA"; return apStarted_ ? "AP" : "Подключение"; }
String NetworkManager::ssid() const { return WiFi.status() == WL_CONNECTED ? WiFi.SSID() : apSsid_; }
String NetworkManager::ip() const { return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : (apStarted_ ? WiFi.softAPIP().toString() : "-"); }
