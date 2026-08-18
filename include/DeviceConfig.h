#pragma once

#include <Arduino.h>

struct DeviceConfig {
  uint16_t motorStepsPerRev = 200;
  uint16_t microsteps = 8;
  float gearboxRatio = 10.0F;
  float screwLeadMm = 5.0F;
  float stepsPerMmOverride = 0.0F;
  bool directionInverted = false;
  int8_t measurementSign = 1;
  float calibrationLengthMm = 100.0F;

  float manualLeftSpeedMmS = 2.0F;
  float measurementSpeedMmS = 1.5F;
  float fineMeasurementSpeedMmS = 0.20F;
  float calibrationSpeedMmS = 1.0F;
  float fineCalibrationSpeedMmS = 0.15F;
  float accelerationMmSS = 10.0F;
  bool doubleTouchEnabled = true;
  float contactBackoffMm = 0.5F;

  uint16_t buttonDebounceMs = 30;
  uint16_t contactDebounceMs = 10;
  uint16_t needleDetectDebounceMs = 100;
  uint16_t needleRemovedDebounceMs = 300;
  uint16_t autoMeasurementDelayMs = 1500;
  bool automaticModeEnabled = false;

  float maxMeasurementTravelMm = 180.0F;
  float maxCalibrationTravelMm = 180.0F;
  uint32_t maxMeasurementTimeMs = 180000;
  uint32_t maxCalibrationTimeMs = 180000;
  uint32_t maxContinuousManualLeftTimeMs = 30000;
  float deviationGreenPercent = 0.5F;
  float deviationRedPercent = 2.0F;

  String wifiSsid;
  String wifiPassword;
  uint32_t wifiConnectTimeoutMs = 15000;
  uint32_t wifiRetryIntervalMs = 60000;
  String fallbackApSsid = "NeedleGauge";
  String fallbackApPassword = "NeedleGauge2026";
};

inline float stepsPerMm(const DeviceConfig &c) {
  return c.stepsPerMmOverride > 0.0F ? c.stepsPerMmOverride
      : (static_cast<float>(c.motorStepsPerRev) * c.microsteps * c.gearboxRatio / c.screwLeadMm);
}
