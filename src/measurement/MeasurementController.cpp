#include "MeasurementController.h"
#include <cmath>

void MeasurementController::begin(const DeviceConfig &config) { config_ = config; setState(MachineState::NEEDS_CALIBRATION); }
void MeasurementController::applyConfig(const DeviceConfig &config, bool resetCalibration) {
  config_ = config;
  if (resetCalibration) { isCalibrated_ = false; hasResult_ = false; setState(MachineState::NEEDS_CALIBRATION); lastMessage_ = "Параметры изменены: требуется калибровка"; }
}

void MeasurementController::setState(MachineState value) { state_ = value; }

bool MeasurementController::isApproach() const { return state_ == MachineState::MEASURING_APPROACH || state_ == MachineState::CALIBRATING_APPROACH; }
bool MeasurementController::isFine() const { return state_ == MachineState::MEASURING_FINE || state_ == MachineState::CALIBRATING_FINE; }
bool MeasurementController::isOperationState() const { return isApproach() || isFine() || state_ == MachineState::MEASURING_VERIFY || state_ == MachineState::CALIBRATING_VERIFY || state_ == MachineState::MEASURING_BACKOFF || state_ == MachineState::CALIBRATING_BACKOFF; }
float MeasurementController::approachSpeed() const { return operation_ == Operation::CALIBRATION ? config_.calibrationSpeedMmS : config_.measurementSpeedMmS; }
float MeasurementController::fineSpeed() const { return operation_ == Operation::CALIBRATION ? config_.fineCalibrationSpeedMmS : config_.fineMeasurementSpeedMmS; }
float MeasurementController::travelLimitMm() const { return operation_ == Operation::CALIBRATION ? config_.maxCalibrationTravelMm : config_.maxMeasurementTravelMm; }
uint32_t MeasurementController::timeoutMs() const { return operation_ == Operation::CALIBRATION ? config_.maxCalibrationTimeMs : config_.maxMeasurementTimeMs; }

void MeasurementController::startMeasurement() { startOperation(Operation::MEASUREMENT); }
void MeasurementController::startCalibration() { startOperation(Operation::CALIBRATION); }

bool MeasurementController::startMotion(int8_t direction, float targetSpeed) {
  motionTargetSpeedMmS_ = targetSpeed;
  motionSpeedMmS_ = config_.accelerationMmSS > 0.0F ? min(targetSpeed, 0.10F) : targetSpeed;
  lastAccelerationAt_ = millis();
  return motor_.start(direction, motionSpeedMmS_);
}
void MeasurementController::updateAcceleration() {
  if (!motor_.isMoving() || config_.accelerationMmSS <= 0.0F || motionSpeedMmS_ >= motionTargetSpeedMmS_) return;
  const uint32_t now = millis(); const float dt = (now - lastAccelerationAt_) / 1000.0F;
  if (dt <= 0.0F) return;
  motionSpeedMmS_ = min(motionTargetSpeedMmS_, motionSpeedMmS_ + config_.accelerationMmSS * dt);
  motor_.setSpeedMmS(motionSpeedMmS_); lastAccelerationAt_ = now;
}

void MeasurementController::startOperation(Operation op) {
  if (state_ == MachineState::MANUAL_LEFT || isOperationState()) return;
  if (op == Operation::MEASUREMENT && !isCalibrated_) { setState(MachineState::NEEDS_CALIBRATION); error_ = "Требуется калибровка"; return; }
  if (inputs_.contactClosed()) { fail("Измерительный контакт уже замкнут. Отведите каретку влево."); return; }
  InputManager::contactInterrupt = false;
  operation_ = op; error_ = ""; operationStartedAt_ = millis(); operationStartSteps_ = motor_.positionSteps();
  const bool started = startMotion(1, approachSpeed());
  if (!started) { fail("Не удалось запустить двигатель"); return; }
  setState(op == Operation::CALIBRATION ? MachineState::CALIBRATING_APPROACH : MachineState::MEASURING_APPROACH);
  lastMessage_ = op == Operation::CALIBRATION ? "Калибровка..." : "Измерение...";
  Serial.printf(op == Operation::CALIBRATION ? "[CALIBRATION] Started\n" : "[MEASURE] Started\n");
}

void MeasurementController::stopByOperator() {
  motor_.stop(); operation_ = Operation::NONE; error_ = ""; lastMessage_ = "Остановлено оператором";
  setState(isCalibrated_ ? MachineState::HOLD : MachineState::NEEDS_CALIBRATION);
  InputManager::stopInterrupt = false; InputManager::contactInterrupt = false;
}

void MeasurementController::fail(const String &message) {
  motor_.stop(); operation_ = Operation::NONE; error_ = message; lastMessage_ = "Ошибка: " + message; setState(MachineState::ERROR); InputManager::contactInterrupt = false;
  Serial.printf("[ERROR] %s\n", message.c_str());
}

void MeasurementController::onFirstContact() {
  firstContactSteps_ = motor_.positionSteps();
  Serial.printf("[MEASURE] First contact at %lld steps\n", firstContactSteps_);
  if (!config_.doubleTouchEnabled || isFine()) { finalContact(); return; }
  const int64_t backoff = static_cast<int64_t>(llround(config_.contactBackoffMm * stepsPerMm(config_)));
  backoffTargetSteps_ = firstContactSteps_ - backoff;
  if (!startMotion(-1, fineSpeed())) { fail("Не удалось выполнить отъезд"); return; }
  setState(operation_ == Operation::CALIBRATION ? MachineState::CALIBRATING_BACKOFF : MachineState::MEASURING_BACKOFF);
  Serial.printf("[MEASURE] Backoff %.3f mm\n", config_.contactBackoffMm);
}

void MeasurementController::finalContact() {
  const int64_t position = motor_.positionSteps(); motor_.stop();
  if (operation_ == Operation::CALIBRATION) {
    calibrationPositionSteps_ = position; isCalibrated_ = true; hasResult_ = false; lastMessage_ = "Калибровка завершена";
    Serial.printf("[CALIBRATION] Final contact at %lld steps\n", position);
  } else {
    measurementPositionSteps_ = position; calculateResult(); measurementCount_++; lastMessage_ = "Измерение завершено";
    Serial.printf("[MEASURE] Final contact at %lld steps; Length = %.3f mm\n", position, lastLengthMm_);
  }
  operation_ = Operation::NONE; setState(MachineState::HOLD);
}

void MeasurementController::calculateResult() {
  lastLengthMm_ = config_.calibrationLengthMm + config_.measurementSign * static_cast<float>(calibrationPositionSteps_ - measurementPositionSteps_) / stepsPerMm(config_);
  deviationMm_ = lastLengthMm_ - config_.calibrationLengthMm;
  deviationPercent_ = deviationMm_ / config_.calibrationLengthMm * 100.0F; hasResult_ = true;
  Serial.printf("[MEASURE] Deviation = %+.3f mm (%+.3f %%)\n", deviationMm_, deviationPercent_);
}

void MeasurementController::handleEvent(InputEvent event) {
  switch (event) {
    case InputEvent::STOP: stopByOperator(); return;
    case InputEvent::LEFT_DOWN:
      motor_.stop(); operation_ = Operation::NONE; error_ = ""; manualStartedAt_ = millis(); motor_.start(-1, config_.manualLeftSpeedMmS); setState(MachineState::MANUAL_LEFT); lastMessage_ = "Ручной отъезд влево"; return;
    case InputEvent::LEFT_UP: if (state_ == MachineState::MANUAL_LEFT) { motor_.stop(); setState(isCalibrated_ ? MachineState::HOLD : MachineState::NEEDS_CALIBRATION); lastMessage_ = "Удержание"; } return;
    case InputEvent::MEASURE: startMeasurement(); return;
    case InputEvent::CALIBRATE: startCalibration(); return;
    case InputEvent::NEEDLE_INSERTED:
      if (config_.automaticModeEnabled && autoArmed_) { autoArmed_ = false; if (isCalibrated_) { autoWaitStartedAt_ = millis(); setState(MachineState::AUTO_WAIT); lastMessage_ = "Ожидание автоматического измерения"; } else { lastMessage_ = "Игла обнаружена; требуется калибровка"; } } return;
    case InputEvent::NEEDLE_REMOVED: autoArmed_ = true; if (state_ == MachineState::AUTO_WAIT) { setState(isCalibrated_ ? MachineState::IDLE : MachineState::NEEDS_CALIBRATION); lastMessage_ = "Автозапуск отменён: игла удалена"; } return;
    default: return;
  }
}

void MeasurementController::update() {
  if (InputManager::stopInterrupt) { handleEvent(InputEvent::STOP); return; }
  if (InputManager::contactInterrupt && (isApproach() || isFine())) {
    InputManager::contactInterrupt = false; contactSeenAt_ = millis(); motor_.stop();
    setState(operation_ == Operation::CALIBRATION ? MachineState::CALIBRATING_VERIFY : MachineState::MEASURING_VERIFY); return;
  }
  const uint32_t now = millis();
  if (state_ == MachineState::MANUAL_LEFT && now - manualStartedAt_ >= config_.maxContinuousManualLeftTimeMs) { motor_.stop(); lastMessage_ = "Лимит ручного отъезда; отпустите и нажмите кнопку снова"; setState(isCalibrated_ ? MachineState::HOLD : MachineState::NEEDS_CALIBRATION); return; }
  if (state_ == MachineState::AUTO_WAIT) { if (!inputs_.needleDetected()) { setState(isCalibrated_ ? MachineState::IDLE : MachineState::NEEDS_CALIBRATION); return; } if (now - autoWaitStartedAt_ >= config_.autoMeasurementDelayMs) startMeasurement(); return; }
  if (state_ == MachineState::MEASURING_VERIFY || state_ == MachineState::CALIBRATING_VERIFY) {
    if (now - contactSeenAt_ < config_.contactDebounceMs) return;
    if (inputs_.contactClosed()) onFirstContact();
    else { const float cautious = max(fineSpeed(), approachSpeed() * 0.25F); startMotion(1, cautious); setState(operation_ == Operation::CALIBRATION ? MachineState::CALIBRATING_APPROACH : MachineState::MEASURING_APPROACH); }
    return;
  }
  if (state_ == MachineState::MEASURING_BACKOFF || state_ == MachineState::CALIBRATING_BACKOFF) {
    if (motor_.positionSteps() <= backoffTargetSteps_) { motor_.stop(); if (inputs_.contactClosed()) { fail("Контакт не разомкнулся после отъезда"); return; } if (!startMotion(1, fineSpeed())) { fail("Не удалось начать точное касание"); return; } setState(operation_ == Operation::CALIBRATION ? MachineState::CALIBRATING_FINE : MachineState::MEASURING_FINE); lastMessage_ = "Точное касание..."; }
    return;
  }
  if (isOperationState()) {
    updateAcceleration();
    const float travel = static_cast<float>(motor_.positionSteps() - operationStartSteps_) / stepsPerMm(config_);
    if (travel > travelLimitMm() || now - operationStartedAt_ >= timeoutMs()) { fail("Контакт не обнаружен"); return; }
  }
}

String MeasurementController::stateName() const {
  switch (state_) { case MachineState::NEEDS_CALIBRATION:return "NEEDS_CALIBRATION"; case MachineState::IDLE:return "IDLE"; case MachineState::AUTO_WAIT:return "AUTO_WAIT"; case MachineState::MANUAL_LEFT:return "MANUAL_LEFT"; case MachineState::MEASURING_APPROACH:return "MEASURING_APPROACH"; case MachineState::MEASURING_VERIFY:return "MEASURING_VERIFY"; case MachineState::MEASURING_BACKOFF:return "MEASURING_BACKOFF"; case MachineState::MEASURING_FINE:return "MEASURING_FINE"; case MachineState::CALIBRATING_APPROACH:return "CALIBRATING_APPROACH"; case MachineState::CALIBRATING_VERIFY:return "CALIBRATING_VERIFY"; case MachineState::CALIBRATING_BACKOFF:return "CALIBRATING_BACKOFF"; case MachineState::CALIBRATING_FINE:return "CALIBRATING_FINE"; case MachineState::HOLD:return "HOLD"; default:return "ERROR"; }
}
String MeasurementController::stateText() const {
  if (state_ == MachineState::ERROR) return lastMessage_; if (state_ == MachineState::NEEDS_CALIBRATION) return "Требуется калибровка"; if (state_ == MachineState::AUTO_WAIT) return "Измерение начнётся через " + String(autoWaitRemainingMs() / 1000.0F, 1) + " с"; if (state_ == MachineState::MANUAL_LEFT) return "Ручной отъезд влево"; if (isFine()) return "Точное касание..."; if (isApproach()) return operation_ == Operation::CALIBRATION ? "Калибровка..." : "Измерение..."; if (state_ == MachineState::HOLD) return lastMessage_.length() ? lastMessage_ : "Удержание"; return "Готов";
}
uint32_t MeasurementController::autoWaitRemainingMs() const { if (state_ != MachineState::AUTO_WAIT) return 0; const uint32_t elapsed = millis() - autoWaitStartedAt_; return elapsed >= config_.autoMeasurementDelayMs ? 0 : config_.autoMeasurementDelayMs - elapsed; }
void MeasurementController::toJson(JsonObject out) const {
  out["state"] = stateName(); out["stateText"] = stateText(); out["error"] = error_; out["calibrated"] = isCalibrated_; out["needleDetected"] = inputs_.needleDetected(); out["contactClosed"] = inputs_.contactClosed(); out["motorEnabled"] = motor_.isEnabled(); out["motorMoving"] = motor_.isMoving(); out["direction"] = motor_.direction() > 0 ? "RIGHT" : motor_.direction() < 0 ? "LEFT" : "STOPPED"; out["measurementCount"] = measurementCount_; out["autoWaitRemainingMs"] = autoWaitRemainingMs();
  if (hasResult_) { out["lastLengthMm"] = lastLengthMm_; out["deviationMm"] = deviationMm_; out["deviationPercent"] = deviationPercent_; out["absoluteDeviationPercent"] = fabsf(deviationPercent_); } else { out["lastLengthMm"] = nullptr; out["deviationMm"] = nullptr; out["deviationPercent"] = nullptr; out["absoluteDeviationPercent"] = nullptr; }
}
