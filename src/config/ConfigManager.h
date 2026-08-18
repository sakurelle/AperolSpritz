#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "DeviceConfig.h"

class ConfigManager {
 public:
  void begin();
  const DeviceConfig &get() const { return config_; }
  DeviceConfig &getMutable() { return config_; }
  bool loadConfig();
  bool saveConfig();
  void resetConfigToDefaults();
  bool validateConfig(const DeviceConfig &candidate, String &error) const;
  void toJson(JsonObject out, bool includePassword = false) const;
  bool updateFromJson(JsonObjectConst in, String &error, bool &mechanicsChanged, bool &wifiChanged);

 private:
  DeviceConfig config_;
};
