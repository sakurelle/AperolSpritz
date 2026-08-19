#pragma once

#include "driver/gpio.h"

namespace Pins {
constexpr gpio_num_t LEFT_PIN = GPIO_NUM_2; constexpr gpio_num_t STOP_PIN = GPIO_NUM_1; constexpr gpio_num_t MEASURE_PIN = GPIO_NUM_0;
constexpr gpio_num_t STEP_PIN = GPIO_NUM_3; constexpr gpio_num_t DIR_PIN = GPIO_NUM_4; constexpr gpio_num_t EN_PIN = GPIO_NUM_10;
constexpr gpio_num_t CONTACT_PIN = GPIO_NUM_20; constexpr gpio_num_t CALIBRATE_PIN = GPIO_NUM_21; constexpr gpio_num_t NEEDLE_PIN = GPIO_NUM_8;
}  // namespace Pins
