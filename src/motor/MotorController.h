#pragma once

#include <Arduino.h>
#include "DeviceConfig.h"

class MotorController {
 public:
  void begin(const DeviceConfig &config);
  void applyConfig(const DeviceConfig &config);
  bool start(int8_t logicalDirection, float speedMmS);
  bool setSpeedMmS(float speedMmS);
  void stop();
  void emergencyStopFromIsr();
  static void IRAM_ATTR emergencyStopActiveFromIsr();
  static void IRAM_ATTR stopRightActiveFromIsr();
  bool isMoving() const { return moving_; }
  bool isEnabled() const { return enabled_; }
  int8_t direction() const { return logicalDirection_; }
  int64_t positionSteps() const;
  int64_t startPositionSteps() const { return startPositionSteps_; }
  uint64_t stepsSinceStart() const;
  void setPositionSteps(int64_t value);

 private:
  static void IRAM_ATTR onTimer();
  static MotorController *instance_;
  hw_timer_t *timer_ = nullptr;
  volatile int64_t positionSteps_ = 0;
  volatile uint64_t stepsSinceStart_ = 0;
  volatile bool stepHigh_ = false;
  volatile bool moving_ = false;
  bool enabled_ = false;
  int8_t logicalDirection_ = 0;
  int64_t startPositionSteps_ = 0;
  DeviceConfig config_;
};
