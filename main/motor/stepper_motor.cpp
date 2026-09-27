#include "motor/stepper_motor.hpp"

#include "config/pins.hpp"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

namespace {
constexpr char TAG[] = "stepper";
constexpr uint32_t MIN_START_SPEED_STEPS_S = 200;
}

bool IRAM_ATTR StepperMotor::on_alarm(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *arg) {
    auto *self = static_cast<StepperMotor *>(arg);
    if (!self->running_ || self->abort_requested_ || self->contact_abort_requested_ || self->limit_reached_) {
        gpio_set_level(STEP_PIN, 0);
        return false;
    }
    if (self->pulse_high_) {
        gpio_set_level(STEP_PIN, 0);
        self->pulse_high_ = false;
        return false;
    }
    if (self->completed_steps_ >= self->max_steps_) {
        self->limit_reached_ = true;
        gpio_set_level(STEP_PIN, 0);
        return false;
    }
    gpio_set_level(STEP_PIN, 1);
    self->pulse_high_ = true;
    self->completed_steps_ = self->completed_steps_ + 1;
    portENTER_CRITICAL_ISR(&self->position_lock_);
    self->position_steps_ += self->direction_positive_ ? 1 : -1;
    portEXIT_CRITICAL_ISR(&self->position_lock_);
    return false;
}

esp_err_t StepperMotor::init() {
    gpio_config_t io{};
    io.pin_bit_mask = (1ULL << STEP_PIN) | (1ULL << DIR_PIN) | (1ULL << EN_PIN);
    io.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "GPIO config failed");
    gpio_set_level(STEP_PIN, 0);
    gpio_set_level(DIR_PIN, 0);
    gpio_set_level(EN_PIN, 1); // TMC2209 enable is active LOW.

    gptimer_config_t cfg{};
    cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    cfg.direction = GPTIMER_COUNT_UP;
    cfg.resolution_hz = 1000000;
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&cfg, &timer_), TAG, "timer create failed");
    gptimer_event_callbacks_t callbacks{};
    callbacks.on_alarm = on_alarm;
    ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(timer_, &callbacks, this), TAG, "timer callback failed");
    return gptimer_enable(timer_);
}

esp_err_t StepperMotor::configure_step_period(uint32_t speed_steps_s) {
    if (speed_steps_s == 0) return ESP_ERR_INVALID_ARG;
    const uint32_t requested_period = 1000000UL / speed_steps_s;
    const uint32_t period_us = requested_period > (2 * step_high_us_) ? requested_period : (2 * step_high_us_);
    const uint32_t half_period_us = (period_us / 2) > step_high_us_ ? (period_us / 2) : step_high_us_;
    gptimer_alarm_config_t alarm{};
    alarm.alarm_count = half_period_us;
    alarm.flags.auto_reload_on_alarm = true;
    return gptimer_set_alarm_action(timer_, &alarm);
}

esp_err_t StepperMotor::start(bool direction_positive, uint32_t target_speed_steps_s, uint32_t acceleration_steps_s2,
                               uint32_t max_steps, uint32_t high_us, bool stop_on_contact) {
    if (!timer_ || target_speed_steps_s == 0 || max_steps == 0 || stop_latched_) return ESP_ERR_INVALID_STATE;
    if (timer_started_) {
        ESP_RETURN_ON_ERROR(gptimer_stop(timer_), TAG, "timer stop failed");
        timer_started_ = false;
    }
    step_high_us_ = high_us;
    target_speed_steps_s_ = target_speed_steps_s;
    current_speed_steps_s_ = target_speed_steps_s < MIN_START_SPEED_STEPS_S ? target_speed_steps_s : MIN_START_SPEED_STEPS_S;
    acceleration_steps_s2_ = acceleration_steps_s2;
    max_steps_ = max_steps;
    completed_steps_ = 0;
    pulse_high_ = false;
    limit_reached_ = false;
    abort_requested_ = false;
    contact_abort_requested_ = false;
    stop_on_contact_ = stop_on_contact;
    direction_positive_ = direction_positive;
    gpio_set_level(STEP_PIN, 0);
    gpio_set_level(DIR_PIN, direction_positive ? 1 : 0);
    gpio_set_level(EN_PIN, 0);
    enabled_ = true;
    ESP_RETURN_ON_ERROR(gptimer_set_raw_count(timer_, 0), TAG, "timer reset failed");
    ESP_RETURN_ON_ERROR(configure_step_period(current_speed_steps_s_), TAG, "alarm configure failed");
    running_ = true;
    const esp_err_t err = gptimer_start(timer_);
    if (err == ESP_OK) {
        timer_started_ = true;
        return ESP_OK;
    }
    running_ = false;
    gpio_set_level(STEP_PIN, 0);
    gpio_set_level(EN_PIN, 1);
    enabled_ = false;
    return err;
}

void StepperMotor::ramp_tick(uint32_t elapsed_ms) {
    if (!running_ || current_speed_steps_s_ >= target_speed_steps_s_ || acceleration_steps_s2_ == 0) return;
    uint32_t increment = static_cast<uint32_t>((static_cast<uint64_t>(acceleration_steps_s2_) * elapsed_ms) / 1000ULL);
    if (increment == 0) increment = 1;
    const uint32_t next = (target_speed_steps_s_ - current_speed_steps_s_ <= increment)
        ? target_speed_steps_s_ : current_speed_steps_s_ + increment;
    if (configure_step_period(next) == ESP_OK) current_speed_steps_s_ = next;
}

void StepperMotor::stop() {
    running_ = false;
    stop_on_contact_ = false;
    contact_abort_requested_ = false;
    if (timer_ && timer_started_) {
        gptimer_stop(timer_);
        timer_started_ = false;
    }
    gpio_set_level(STEP_PIN, 0);
    gpio_set_level(EN_PIN, 1);
    enabled_ = false;
}

void IRAM_ATTR StepperMotor::emergency_stop_isr() {
    stop_latched_ = true;
    abort_requested_ = true;
    contact_abort_requested_ = false;
    running_ = false;
    stop_on_contact_ = false;
    gpio_set_level(STEP_PIN, 0);
    gpio_set_level(EN_PIN, 1);
    enabled_ = false;
}

void IRAM_ATTR StepperMotor::request_stop_isr() {
    abort_requested_ = true;
    gpio_set_level(STEP_PIN, 0);
}

void IRAM_ATTR StepperMotor::request_contact_abort_isr() {
    if (stop_on_contact_) {
        contact_abort_requested_ = true;
        gpio_set_level(STEP_PIN, 0);
    }
}

bool StepperMotor::clear_contact_abort() {
    if (stop_latched_ || abort_requested_ || limit_reached_) return false;
    contact_abort_requested_ = false;
    return true;
}

void StepperMotor::clear_abort() {
    abort_requested_ = false;
    contact_abort_requested_ = false;
    limit_reached_ = false;
}

void StepperMotor::clear_stop_latch() { stop_latched_ = false; }

int64_t StepperMotor::position_steps() const {
    portENTER_CRITICAL(&position_lock_);
    const int64_t value = position_steps_;
    portEXIT_CRITICAL(&position_lock_);
    return value;
}
