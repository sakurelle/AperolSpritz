#include "WebServerManager.h"
#include <ArduinoJson.h>
#include "WebPage.h"

void WebServerManager::begin() {
  server_.on("/", HTTP_GET, [this]{ server_.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  server_.on("/api/status", HTTP_GET, [this]{ sendStatus(); }); server_.on("/api/config", HTTP_GET, [this]{ sendConfig(); });
  server_.on("/api/config", HTTP_PUT, [this]{ updateConfig(); }); server_.on("/api/config/reset", HTTP_POST, [this]{ resetConfig(); });
  server_.on("/api/network/reset", HTTP_POST, [this]{ resetLocalNetwork(); });
  server_.onNotFound([this]{ server_.send(404, "application/json", "{\"error\":\"Не найдено\"}"); }); server_.begin(); Serial.println("[WIFI] Web server started on port 80");
}
void WebServerManager::sendStatus() { JsonDocument doc; JsonObject out = doc.to<JsonObject>(); measurement_.toJson(out); out["wifiMode"] = network_.modeName(); out["ssid"] = network_.ssid(); out["ip"] = network_.ip(); out["connectedClients"] = network_.connectedClients(); const DeviceConfig &c = config_.get(); out["deviationGreenPercent"] = c.deviationGreenPercent; out["deviationRedPercent"] = c.deviationRedPercent; String body; serializeJson(doc, body); server_.send(200, "application/json", body); }
void WebServerManager::sendConfig() { JsonDocument doc; config_.toJson(doc.to<JsonObject>(), true); String body; serializeJson(doc, body); server_.send(200, "application/json", body); }
void WebServerManager::updateConfig() {
  JsonDocument doc; const DeserializationError parse = deserializeJson(doc, server_.arg("plain")); if (parse) { server_.send(400, "application/json", "{\"error\":\"Некорректный JSON\"}"); return; }
  String error; bool mechanics = false, wifi = false; if (!config_.updateFromJson(doc.as<JsonObjectConst>(), error, mechanics, wifi)) { JsonDocument out; out["error"] = error; String body; serializeJson(out, body); server_.send(400, "application/json", body); return; }
  config_.saveConfig(); motor_.applyConfig(config_.get()); inputs_.applyConfig(config_.get()); measurement_.applyConfig(config_.get(), mechanics);
  JsonDocument out; out["message"] = mechanics ? "Настройки сохранены; требуется повторная калибровка." : (wifi ? "Настройки AP сохранены. Связь будет разорвана через секунду; подключитесь к новой сети." : "Настройки сохранены."); String body; serializeJson(out, body); server_.send(200, "application/json", body);
}
void WebServerManager::resetConfig() { config_.resetConfigToDefaults(); config_.saveConfig(); motor_.applyConfig(config_.get()); inputs_.applyConfig(config_.get()); measurement_.applyConfig(config_.get(), true); server_.send(200, "application/json", "{\"message\":\"Настройки сброшены; требуется калибровка.\"}"); }
void WebServerManager::resetLocalNetwork() { DeviceConfig &c = config_.getMutable(); c.apSsidPrefix = "NeedleGauge"; c.apPassword = "NeedleGauge2026"; config_.saveConfig(); server_.send(200, "application/json", "{\"message\":\"Настройки локальной сети восстановлены; связь будет разорвана через секунду.\"}"); }
