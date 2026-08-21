#include "motor/stepper_motor.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include <algorithm>

namespace { constexpr gpio_num_t STEP_PIN = GPIO_NUM_3, DIR_PIN = GPIO_NUM_4, EN_PIN = GPIO_NUM_10; constexpr const char *TAG = "stepper"; }

bool IRAM_ATTR StepperMotor::on_alarm(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *arg) {
    auto *self = static_cast<StepperMotor *>(arg);
    if (!self->running_ || self->abort_requested_ || self->limit_reached_) {
        gpio_set_level(STEP_PIN, 0);
        return false;
    }
    if (self->pulse_high_) {
        gpio_set_level(STEP_PIN, 0);
        self->pulse_high_ = false;
    } else {
        if (self->completed_steps_ >= self->max_steps_) {
            self->limit_reached_ = true;
            gpio_set_level(STEP_PIN, 0);
            return false;
        }
        gpio_set_level(STEP_PIN, 1); // Count one selected (rising) STEP edge.
        self->pulse_high_ = true;
        self->completed_steps_ = self->completed_steps_ + 1;
    }
    return false;
}

esp_err_t StepperMotor::init() {
    gpio_config_t io{}; io.pin_bit_mask = (1ULL << STEP_PIN) | (1ULL << DIR_PIN) | (1ULL << EN_PIN); io.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "GPIO config failed");
    gpio_set_level(STEP_PIN, 0); gpio_set_level(DIR_PIN, 0); gpio_set_level(EN_PIN, 1); // active-low EN: safe disabled
    gptimer_config_t cfg{}; cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT; cfg.direction = GPTIMER_COUNT_UP; cfg.resolution_hz = 1000000;
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&cfg, &timer_), TAG, "timer create failed");
    gptimer_event_callbacks_t callbacks{}; callbacks.on_alarm = on_alarm;
    ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(timer_, &callbacks, this), TAG, "timer callback failed");
    ESP_RETURN_ON_ERROR(gptimer_enable(timer_), TAG, "timer enable failed");
    return ESP_OK;
}

esp_err_t StepperMotor::start(bool direction, uint32_t speed, uint32_t max_steps, uint32_t high_us) {
    if (!timer_ || speed == 0 || max_steps == 0 || stop_latched_) return ESP_ERR_INVALID_STATE;
    // Timer period is a complete STEP cycle; high phase is guaranteed separately by validation.
    uint32_t period_us = std::max<uint32_t>(2 * high_us, 1000000UL / speed);
    uint32_t half_period_us = std::max<uint32_t>(high_us, period_us / 2);
    gptimer_alarm_config_t alarm{}; alarm.alarm_count = half_period_us; alarm.flags.auto_reload_on_alarm = true;
    if (timer_started_) { ESP_RETURN_ON_ERROR(gptimer_stop(timer_), TAG, "timer stop failed"); timer_started_ = false; }
    gpio_set_level(STEP_PIN, 0); gpio_set_level(DIR_PIN, direction ? 1 : 0);
    completed_steps_ = 0; max_steps_ = max_steps; pulse_high_ = false; limit_reached_ = false; abort_requested_ = false;
    gpio_set_level(EN_PIN, 0); enabled_ = true;
    ESP_RETURN_ON_ERROR(gptimer_set_raw_count(timer_, 0), TAG, "timer reset failed");
    ESP_RETURN_ON_ERROR(gptimer_set_alarm_action(timer_, &alarm), TAG, "alarm configure failed");
    running_ = true;
    esp_err_t err = gptimer_start(timer_);
    if (err == ESP_OK) timer_started_ = true;
    if (err != ESP_OK || stop_latched_) { running_ = false; if (timer_started_) { gptimer_stop(timer_); timer_started_ = false; } gpio_set_level(EN_PIN, 1); enabled_ = false; return err == ESP_OK ? ESP_ERR_INVALID_STATE : err; }
    return err;
}
void StepperMotor::stop() {
    running_ = false; if (timer_ && timer_started_) { gptimer_stop(timer_); timer_started_ = false; } gpio_set_level(STEP_PIN, 0); gpio_set_level(EN_PIN, 1); enabled_ = false;
}
void IRAM_ATTR StepperMotor::emergency_stop_isr() {
    stop_latched_ = true; abort_requested_ = true; running_ = false; gpio_set_level(STEP_PIN, 0); gpio_set_level(EN_PIN, 1); enabled_ = false;
}
void IRAM_ATTR StepperMotor::request_stop_isr() { abort_requested_ = true; gpio_set_level(STEP_PIN, 0); }
void StepperMotor::clear_abort() { abort_requested_ = false; limit_reached_ = false; }
void StepperMotor::clear_stop_latch() { stop_latched_ = false; }
