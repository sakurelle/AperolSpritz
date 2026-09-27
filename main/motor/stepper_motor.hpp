#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

class StepperMotor {
public:
    esp_err_t init();
    esp_err_t start(bool direction_positive, uint32_t target_speed_steps_s, uint32_t acceleration_steps_s2, uint32_t max_steps, uint32_t high_us, bool stop_on_contact);
    void stop();
    void emergency_stop_isr();
    void request_stop_isr();
    void request_contact_abort_isr();
    bool clear_contact_abort();
    void ramp_tick(uint32_t elapsed_ms);
    void clear_abort();
    void clear_stop_latch();
    bool is_running() const { return running_; }
    bool abort_requested() const { return abort_requested_; }
    bool contact_abort_requested() const { return contact_abort_requested_; }
    bool can_generate_steps() const { return running_ && !abort_requested_ && !contact_abort_requested_ && !limit_reached_; }
    bool limit_reached() const { return limit_reached_; }
    bool is_enabled() const { return enabled_; }
    bool stop_latched() const { return stop_latched_; }
    uint32_t completed_steps() const { return completed_steps_; }
    int64_t position_steps() const;
    uint32_t current_speed_steps_s() const { return current_speed_steps_s_; }

private:
    static IRAM_ATTR bool on_alarm(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *ctx);
    esp_err_t configure_step_period(uint32_t speed_steps_s);
    gptimer_handle_t timer_ = nullptr;
    volatile bool running_ = false;
    volatile bool timer_started_ = false;
    volatile bool enabled_ = false;
    volatile bool abort_requested_ = false;
    volatile bool contact_abort_requested_ = false;
    volatile bool stop_latched_ = false;
    volatile bool pulse_high_ = false;
    volatile bool limit_reached_ = false;
    volatile bool stop_on_contact_ = false;
    volatile bool direction_positive_ = false;
    volatile uint32_t completed_steps_ = 0;
    volatile uint32_t max_steps_ = 0;
    volatile uint32_t target_speed_steps_s_ = 0;
    volatile uint32_t current_speed_steps_s_ = 0;
    volatile uint32_t acceleration_steps_s2_ = 0;
    uint32_t step_high_us_ = 4;
    volatile int64_t position_steps_ = 0;
    mutable portMUX_TYPE position_lock_ = portMUX_INITIALIZER_UNLOCKED;
};
