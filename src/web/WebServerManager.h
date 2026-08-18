#pragma once
#include <WebServer.h>
#include "ConfigManager.h"
#include "MeasurementController.h"
#include "NetworkManager.h"

class WebServerManager {
 public:
  WebServerManager(ConfigManager &config, MeasurementController &measurement, NetworkManager &network, MotorController &motor, InputManager &inputs) : config_(config), measurement_(measurement), network_(network), motor_(motor), inputs_(inputs), server_(80) {}
  void begin(); void update() { server_.handleClient(); }
 private:
  void sendStatus(); void sendConfig(); void updateConfig(); void resetConfig();
  ConfigManager &config_; MeasurementController &measurement_; NetworkManager &network_; MotorController &motor_; InputManager &inputs_; WebServer server_;
};
