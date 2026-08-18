#pragma once

#include <Arduino.h>
#include "DeviceConfig.h"

class NetworkManager {
 public:
  void begin(const DeviceConfig &config);
  void update();
  void requestReconnect(const DeviceConfig &config);
  String modeName() const;
  String ssid() const;
  String ip() const;
  String apSsid() const { return apSsid_; }

 private:
  void startAp();
  void beginStation();
  DeviceConfig config_; String apSsid_; bool stationRequested_ = false, apStarted_ = false;
  uint32_t stationStartedAt_ = 0, lastRetryAt_ = 0;
};
