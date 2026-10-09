#pragma once

#include "driver/gpio.h"

// Physical wiring of the needle meter. Keep every hardware GPIO assignment here.
constexpr gpio_num_t CALIBRATE_PIN = GPIO_NUM_2;
constexpr gpio_num_t MEASURE_PIN = GPIO_NUM_10;
constexpr gpio_num_t STOP_PIN = GPIO_NUM_0;
constexpr gpio_num_t CONTACT_PIN = GPIO_NUM_3;
constexpr gpio_num_t NEEDLE_PIN = GPIO_NUM_4;
constexpr gpio_num_t DIR_PIN = GPIO_NUM_5;
constexpr gpio_num_t STEP_PIN = GPIO_NUM_6;
constexpr gpio_num_t EN_PIN = GPIO_NUM_7;
// GPIO8/GPIO9 are ESP32-C3 strapping pins. External circuitry must not pull
// GPIO9 low while the chip is resetting; their output states are only controlled after boot.
constexpr gpio_num_t BUZZER_PIN = GPIO_NUM_8;
constexpr gpio_num_t STATUS_LED_PIN = GPIO_NUM_9;
constexpr gpio_num_t LEFT_PIN = GPIO_NUM_1;
