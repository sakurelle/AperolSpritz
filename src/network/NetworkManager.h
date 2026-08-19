#pragma once

#include <Arduino.h>
#include "DeviceConfig.h"

class NetworkManager {
 public:
  void begin(const DeviceConfig &config);
  void update();

  String modeName() const { return "AP"; }
  String ssid() const { return apSsid_; }
  String ip() const { return "192.168.4.1"; }

  uint8_t connectedClients() const;
  bool isApStarted() const { return apStarted_; }

 private:
  void startAp();

  DeviceConfig config_;
  String apSsid_;
  bool apStarted_ = false;

  uint32_t lastHealthLogMs_ = 0;
};