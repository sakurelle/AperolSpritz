#include "MotorController.h"
#include "Pins.h"
#include <driver/gpio.h>

MotorController *MotorController::instance_ = nullptr;
namespace { portMUX_TYPE motorMux = portMUX_INITIALIZER_UNLOCKED; }

void MotorController::begin(const DeviceConfig &config) {
  config_ = config;
  pinMode(Pins::EN_PIN, OUTPUT); digitalWrite(Pins::EN_PIN, HIGH);  // Disable power immediately.
  pinMode(Pins::STEP_PIN, OUTPUT); digitalWrite(Pins::STEP_PIN, LOW);
  pinMode(Pins::DIR_PIN, OUTPUT); digitalWrite(Pins::DIR_PIN, LOW);
  timer_ = timerBegin(0, 80, true);  // 1 tick = 1 microsecond on ESP32-C3.
  instance_ = this; timerAttachInterrupt(timer_, &MotorController::onTimer, true);
  enabled_ = true; digitalWrite(Pins::EN_PIN, LOW);  // TMC2209 EN is active LOW.
  Serial.println("[MOTOR] Initialized, outputs enabled/holding");
}

void MotorController::applyConfig(const DeviceConfig &config) { config_ = config; }

bool MotorController::start(int8_t logicalDirection, float speedMmS) {
  if (logicalDirection != 1 && logicalDirection != -1 || speedMmS <= 0.0F || !timer_) return false;
  stop();
  const bool physicalRight = (logicalDirection == 1) != config_.directionInverted;
  digitalWrite(Pins::DIR_PIN, physicalRight ? HIGH : LOW);
  const double stepRate = static_cast<double>(speedMmS) * stepsPerMm(config_);
  if (stepRate < 1.0 || stepRate > 25000.0) { Serial.println("[MOTOR] Refusing unsafe step rate"); return false; }
  const uint64_t calculatedHalfPeriodUs = static_cast<uint64_t>(500000.0 / stepRate);
  const uint64_t halfPeriodUs = calculatedHalfPeriodUs < 20 ? 20 : calculatedHalfPeriodUs;
  portENTER_CRITICAL(&motorMux);
  logicalDirection_ = logicalDirection; startPositionSteps_ = positionSteps_; stepsSinceStart_ = 0; stepHigh_ = false; moving_ = true;
  portEXIT_CRITICAL(&motorMux);
  digitalWrite(Pins::STEP_PIN, LOW);
  timerAlarmWrite(timer_, halfPeriodUs, true); timerAlarmEnable(timer_);
  Serial.printf("[MOTOR] Start %s %.3f mm/s (%.1f steps/s)\n", logicalDirection == 1 ? "RIGHT" : "LEFT", speedMmS, stepRate);
  return true;
}

bool MotorController::setSpeedMmS(float speedMmS) {
  if (!timer_ || !moving_ || speedMmS <= 0.0F) return false;
  const double stepRate = static_cast<double>(speedMmS) * stepsPerMm(config_);
  if (stepRate < 1.0 || stepRate > 25000.0) return false;
  const uint64_t calculated = static_cast<uint64_t>(500000.0 / stepRate);
  timerAlarmWrite(timer_, calculated < 20 ? 20 : calculated, true);
  return true;
}

void MotorController::stop() {
  if (!timer_) return;
  timerAlarmDisable(timer_); digitalWrite(Pins::STEP_PIN, LOW);
  portENTER_CRITICAL(&motorMux); moving_ = false; stepHigh_ = false; logicalDirection_ = 0; portEXIT_CRITICAL(&motorMux);
}

void IRAM_ATTR MotorController::emergencyStopFromIsr() {
  if (!timer_) return;
  timerAlarmDisable(timer_); gpio_set_level(static_cast<gpio_num_t>(Pins::STEP_PIN), 0);
  portENTER_CRITICAL_ISR(&motorMux); moving_ = false; stepHigh_ = false; logicalDirection_ = 0; portEXIT_CRITICAL_ISR(&motorMux);
}

void IRAM_ATTR MotorController::emergencyStopActiveFromIsr() { if (instance_) instance_->emergencyStopFromIsr(); }
void IRAM_ATTR MotorController::stopRightActiveFromIsr() { if (instance_ && instance_->logicalDirection_ > 0) instance_->emergencyStopFromIsr(); }

void IRAM_ATTR MotorController::onTimer() {
  MotorController *self = instance_; if (!self || !self->moving_) return;
  self->stepHigh_ = !self->stepHigh_;
  gpio_set_level(static_cast<gpio_num_t>(Pins::STEP_PIN), self->stepHigh_ ? 1 : 0);
  if (self->stepHigh_) {
    self->positionSteps_ += self->logicalDirection_;
    self->stepsSinceStart_++;
  }
}

int64_t MotorController::positionSteps() const { portENTER_CRITICAL(&motorMux); const int64_t result = positionSteps_; portEXIT_CRITICAL(&motorMux); return result; }
uint64_t MotorController::stepsSinceStart() const { portENTER_CRITICAL(&motorMux); const uint64_t result = stepsSinceStart_; portEXIT_CRITICAL(&motorMux); return result; }
void MotorController::setPositionSteps(int64_t value) { portENTER_CRITICAL(&motorMux); positionSteps_ = value; portEXIT_CRITICAL(&motorMux); }
