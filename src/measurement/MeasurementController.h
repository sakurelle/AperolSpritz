#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "DeviceConfig.h"
#include "MotorController.h"
#include "InputManager.h"

enum class MachineState : uint8_t {
  NEEDS_CALIBRATION, IDLE, AUTO_WAIT, MANUAL_LEFT,
  MEASURING_APPROACH, MEASURING_VERIFY, MEASURING_BACKOFF, MEASURING_FINE,
  CALIBRATING_APPROACH, CALIBRATING_VERIFY, CALIBRATING_BACKOFF, CALIBRATING_FINE,
  HOLD, ERROR
};

class MeasurementController {
 public:
  MeasurementController(MotorController &motor, InputManager &inputs) : motor_(motor), inputs_(inputs) {}
  void begin(const DeviceConfig &config);
  void applyConfig(const DeviceConfig &config, bool resetCalibration);
  void update();
  void handleEvent(InputEvent event);
  void startMeasurement();
  void startCalibration();
  void stopByOperator();
  void toJson(JsonObject out) const;
  bool calibrated() const { return isCalibrated_; }
  MachineState state() const { return state_; }
  String stateName() const;
  String stateText() const;
  String error() const { return error_; }
  uint32_t autoWaitRemainingMs() const;

 private:
  enum class Operation : uint8_t { NONE, MEASUREMENT, CALIBRATION };
  void startOperation(Operation op);
  void onFirstContact();
  void finalContact();
  void fail(const String &message);
  void setState(MachineState state);
  bool isApproach() const;
  bool isFine() const;
  bool isOperationState() const;
  float approachSpeed() const;
  float fineSpeed() const;
  float travelLimitMm() const;
  uint32_t timeoutMs() const;
  void calculateResult();
  bool startMotion(int8_t direction, float targetSpeed);
  void updateAcceleration();

  MotorController &motor_; InputManager &inputs_; DeviceConfig config_;
  MachineState state_ = MachineState::NEEDS_CALIBRATION;
  Operation operation_ = Operation::NONE;
  bool isCalibrated_ = false, autoArmed_ = true;
  uint32_t operationStartedAt_ = 0, contactSeenAt_ = 0, autoWaitStartedAt_ = 0, manualStartedAt_ = 0;
  int64_t operationStartSteps_ = 0, firstContactSteps_ = 0, calibrationPositionSteps_ = 0, measurementPositionSteps_ = 0, backoffTargetSteps_ = 0;
  uint32_t measurementCount_ = 0;
  bool hasResult_ = false; float lastLengthMm_ = NAN, deviationMm_ = NAN, deviationPercent_ = NAN;
  String error_; String lastMessage_;
  float motionSpeedMmS_ = 0.0F, motionTargetSpeedMmS_ = 0.0F;
  uint32_t lastAccelerationAt_ = 0;
};
