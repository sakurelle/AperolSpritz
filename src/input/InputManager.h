#pragma once

#include <Arduino.h>
#include "DeviceConfig.h"

enum class InputEvent : uint8_t { NONE, STOP, LEFT_DOWN, LEFT_UP, MEASURE, CALIBRATE, NEEDLE_INSERTED, NEEDLE_REMOVED };

class InputManager {
 public:
  void begin(const DeviceConfig &config);
  void applyConfig(const DeviceConfig &config) { config_ = config; }
  void update();
  bool contactClosed() const { return digitalRead(contactPin_) == LOW; }
  bool needleDetected() const { return needleStable_ == LOW; }
  InputEvent nextEvent();
  static volatile bool stopInterrupt;
  static volatile bool contactInterrupt;

 private:
  static constexpr uint8_t contactPin_ = 20;
  static void IRAM_ATTR onStop();
  static void IRAM_ATTR onContact();
  void checkButton(uint8_t pin, bool &raw, bool &stable, uint32_t &changedAt, InputEvent down, InputEvent up);
  void queue(InputEvent e);
  DeviceConfig config_;
  bool leftRaw_ = true, leftStable_ = true;
  bool measureRaw_ = true, measureStable_ = true;
  bool calibrateRaw_ = true, calibrateStable_ = true;
  bool needleRaw_ = true, needleStable_ = true;
  uint32_t leftChanged_ = 0, measureChanged_ = 0, calibrateChanged_ = 0, needleChanged_ = 0;
  InputEvent queue_[12]{};
  uint8_t head_ = 0, tail_ = 0;
};
