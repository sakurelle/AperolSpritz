#include "ConfigManager.h"
#include <Preferences.h>

namespace {
constexpr const char *kNamespace = "needle-gauge";

template <typename T> void put(Preferences &p, const char *key, T value);
template <> void put<uint16_t>(Preferences &p, const char *key, uint16_t value) { p.putUShort(key, value); }
template <> void put<uint32_t>(Preferences &p, const char *key, uint32_t value) { p.putUInt(key, value); }
template <> void put<float>(Preferences &p, const char *key, float value) { p.putFloat(key, value); }
template <> void put<bool>(Preferences &p, const char *key, bool value) { p.putBool(key, value); }
template <> void put<int8_t>(Preferences &p, const char *key, int8_t value) { p.putChar(key, value); }

#define CFG_NUMBERS(X) \
 X(motorStepsPerRev, UShort, uint16_t) X(microsteps, UShort, uint16_t) \
 X(gearboxRatio, Float, float) X(screwLeadMm, Float, float) X(stepsPerMmOverride, Float, float) \
 X(directionInverted, Bool, bool) X(measurementSign, Char, int8_t) X(calibrationLengthMm, Float, float) \
 X(manualLeftSpeedMmS, Float, float) X(measurementSpeedMmS, Float, float) X(fineMeasurementSpeedMmS, Float, float) \
 X(calibrationSpeedMmS, Float, float) X(fineCalibrationSpeedMmS, Float, float) X(accelerationMmSS, Float, float) \
 X(doubleTouchEnabled, Bool, bool) X(contactBackoffMm, Float, float) X(buttonDebounceMs, UShort, uint16_t) \
 X(contactDebounceMs, UShort, uint16_t) X(needleDetectDebounceMs, UShort, uint16_t) X(needleRemovedDebounceMs, UShort, uint16_t) \
 X(autoMeasurementDelayMs, UShort, uint16_t) X(automaticModeEnabled, Bool, bool) \
 X(maxMeasurementTravelMm, Float, float) X(maxCalibrationTravelMm, Float, float) \
 X(maxMeasurementTimeMs, UInt, uint32_t) X(maxCalibrationTimeMs, UInt, uint32_t) X(maxContinuousManualLeftTimeMs, UInt, uint32_t) \
 X(deviationGreenPercent, Float, float) X(deviationRedPercent, Float, float)

void jsonField(JsonObject out, const char *name, const String &value) { out[name] = value; }
template <typename T> void jsonField(JsonObject out, const char *name, T value) { out[name] = value; }

bool updateString(JsonObjectConst in, const char *key, String &field) {
  if (!in[key].is<const char *>()) return false;
  field = in[key].as<const char *>(); return true;
}
template <typename T> bool updateNumber(JsonObjectConst in, const char *key, T &field) {
  if (in[key].isNull()) return false;
  field = in[key].as<T>(); return true;
}
}

void ConfigManager::begin() { loadConfig(); }

bool ConfigManager::loadConfig() {
  Preferences p;
  if (!p.begin(kNamespace, true)) return false;
  if (!p.getBool("saved", false)) { p.end(); config_ = DeviceConfig{}; return saveConfig(); }
#define LOAD(name, type, cpp) config_.name = p.get##type(#name, config_.name);
  CFG_NUMBERS(LOAD)
#undef LOAD
  config_.apSsidPrefix = p.getString("apSsidPrefix", config_.apSsidPrefix);
  config_.apPassword = p.getString("apPassword", config_.apPassword);
  p.end();
  String error;
  if (!validateConfig(config_, error)) { Serial.printf("[CONFIG] Invalid NVS config: %s; defaults restored\n", error.c_str()); resetConfigToDefaults(); return saveConfig(); }
  Serial.println("[CONFIG] Loaded from NVS");
  return true;
}

bool ConfigManager::saveConfig() {
  Preferences p;
  if (!p.begin(kNamespace, false)) return false;
#define SAVE(name, type, cpp) put<cpp>(p, #name, config_.name);
  CFG_NUMBERS(SAVE)
#undef SAVE
  p.putString("apSsidPrefix", config_.apSsidPrefix); p.putString("apPassword", config_.apPassword);
  p.putBool("saved", true); p.end(); Serial.println("[CONFIG] Saved to NVS"); return true;
}

void ConfigManager::resetConfigToDefaults() { config_ = DeviceConfig{}; Serial.println("[CONFIG] Defaults restored"); }

bool ConfigManager::validateConfig(const DeviceConfig &c, String &error) const {
  if (c.motorStepsPerRev == 0 || c.microsteps == 0 || c.gearboxRatio <= 0 || c.screwLeadMm <= 0 || c.stepsPerMmOverride < 0) { error = "Некорректные параметры механики"; return false; }
  if (c.measurementSign != 1 && c.measurementSign != -1) { error = "measurementSign должен быть 1 или -1"; return false; }
  if (c.calibrationLengthMm <= 0 || c.manualLeftSpeedMmS < 0 || c.measurementSpeedMmS <= 0 || c.fineMeasurementSpeedMmS <= 0 || c.calibrationSpeedMmS <= 0 || c.fineCalibrationSpeedMmS <= 0 || c.accelerationMmSS < 0) { error = "Некорректная длина или скорость"; return false; }
  if (c.contactBackoffMm < 0 || c.maxMeasurementTravelMm <= 0 || c.maxCalibrationTravelMm <= 0 || c.maxMeasurementTimeMs == 0 || c.maxCalibrationTimeMs == 0 || c.maxContinuousManualLeftTimeMs == 0) { error = "Некорректные пределы движения"; return false; }
  if (c.deviationGreenPercent < 0 || c.deviationRedPercent < c.deviationGreenPercent) { error = "Зелёный порог не может быть больше красного"; return false; }
  if (c.apSsidPrefix.length() == 0 || c.apPassword.length() < 8) { error = "Префикс SSID пуст или пароль AP короче 8 символов"; return false; }
  return true;
}

void ConfigManager::toJson(JsonObject out, bool includePassword) const {
#define JSON(name, type, cpp) jsonField(out, #name, config_.name);
  CFG_NUMBERS(JSON)
#undef JSON
  out["stepsPerMm"] = ::stepsPerMm(config_);
  out["apSsidPrefix"] = config_.apSsidPrefix;
  if (includePassword) out["apPassword"] = config_.apPassword;
}

bool ConfigManager::updateFromJson(JsonObjectConst in, String &error, bool &mechanicsChanged, bool &wifiChanged) {
  DeviceConfig next = config_;
  mechanicsChanged = false; wifiChanged = false;
#define UPDATE(name, type, cpp) updateNumber<cpp>(in, #name, next.name);
  CFG_NUMBERS(UPDATE)
#undef UPDATE
  updateString(in, "apSsidPrefix", next.apSsidPrefix); updateString(in, "apPassword", next.apPassword);
  if (!validateConfig(next, error)) return false;
  mechanicsChanged = next.motorStepsPerRev != config_.motorStepsPerRev || next.microsteps != config_.microsteps || next.gearboxRatio != config_.gearboxRatio || next.screwLeadMm != config_.screwLeadMm || next.stepsPerMmOverride != config_.stepsPerMmOverride || next.directionInverted != config_.directionInverted || next.measurementSign != config_.measurementSign || next.calibrationLengthMm != config_.calibrationLengthMm;
  wifiChanged = next.apSsidPrefix != config_.apSsidPrefix || next.apPassword != config_.apPassword;
  config_ = next; return true;
}
