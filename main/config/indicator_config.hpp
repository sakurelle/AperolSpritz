#pragma once

#include <stdint.h>

namespace indicator_config {

constexpr bool BUZZER_INVERTED = false;
constexpr bool STATUS_LED_INVERTED = true;

constexpr uint32_t BUZZER_START_MS = 60;
constexpr uint32_t BUZZER_SUCCESS_MS = 180;

constexpr uint32_t BUZZER_ERROR_ON_MS = 80;
constexpr uint32_t BUZZER_ERROR_OFF_MS = 90;
constexpr uint32_t BUZZER_ERROR_COUNT = 3;

constexpr uint32_t ERROR_LED_BLINK_MS = 250;

} // namespace indicator_config
