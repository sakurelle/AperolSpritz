#include "InputManager.h"
#include "Pins.h"
#include "MotorController.h"

volatile bool InputManager::stopInterrupt = false;
volatile bool InputManager::contactInterrupt = false;

void IRAM_ATTR InputManager::onStop() { MotorController::emergencyStopActiveFromIsr(); stopInterrupt = true; }
void IRAM_ATTR InputManager::onContact() { MotorController::stopRightActiveFromIsr(); contactInterrupt = true; }

void InputManager::begin(const DeviceConfig &config) {
  config_ = config;
  for (uint8_t pin : {Pins::LEFT_PIN, Pins::STOP_PIN, Pins::MEASURE_PIN, Pins::CONTACT_PIN, Pins::CALIBRATE_PIN, Pins::NEEDLE_PIN}) pinMode(pin, INPUT_PULLUP);
  leftRaw_ = leftStable_ = digitalRead(Pins::LEFT_PIN); measureRaw_ = measureStable_ = digitalRead(Pins::MEASURE_PIN);
  calibrateRaw_ = calibrateStable_ = digitalRead(Pins::CALIBRATE_PIN); needleRaw_ = needleStable_ = digitalRead(Pins::NEEDLE_PIN);
  const uint32_t now = millis(); leftChanged_ = measureChanged_ = calibrateChanged_ = needleChanged_ = now;
  attachInterrupt(digitalPinToInterrupt(Pins::STOP_PIN), &InputManager::onStop, FALLING);
  attachInterrupt(digitalPinToInterrupt(Pins::CONTACT_PIN), &InputManager::onContact, FALLING);
}

void InputManager::queue(InputEvent e) { const uint8_t next = (head_ + 1U) % (sizeof(queue_) / sizeof(queue_[0])); if (next != tail_) { queue_[head_] = e; head_ = next; } }

void InputManager::checkButton(uint8_t pin, bool &raw, bool &stable, uint32_t &changedAt, InputEvent down, InputEvent up) {
  const bool value = digitalRead(pin); const uint32_t now = millis();
  if (value != raw) { raw = value; changedAt = now; }
  if (raw != stable && now - changedAt >= config_.buttonDebounceMs) { stable = raw; queue(stable == LOW ? down : up); }
}

void InputManager::update() {
  checkButton(Pins::LEFT_PIN, leftRaw_, leftStable_, leftChanged_, InputEvent::LEFT_DOWN, InputEvent::LEFT_UP);
  checkButton(Pins::MEASURE_PIN, measureRaw_, measureStable_, measureChanged_, InputEvent::MEASURE, InputEvent::NONE);
  checkButton(Pins::CALIBRATE_PIN, calibrateRaw_, calibrateStable_, calibrateChanged_, InputEvent::CALIBRATE, InputEvent::NONE);
  const bool value = digitalRead(Pins::NEEDLE_PIN); const uint32_t now = millis();
  if (value != needleRaw_) { needleRaw_ = value; needleChanged_ = now; }
  const uint32_t debounce = needleRaw_ == LOW ? config_.needleDetectDebounceMs : config_.needleRemovedDebounceMs;
  if (needleRaw_ != needleStable_ && now - needleChanged_ >= debounce) { needleStable_ = needleRaw_; queue(needleStable_ == LOW ? InputEvent::NEEDLE_INSERTED : InputEvent::NEEDLE_REMOVED); }
}

InputEvent InputManager::nextEvent() {
  if (stopInterrupt) { stopInterrupt = false; return InputEvent::STOP; }
  if (tail_ == head_) return InputEvent::NONE; const InputEvent e = queue_[tail_]; tail_ = (tail_ + 1U) % (sizeof(queue_) / sizeof(queue_[0])); return e;
}
