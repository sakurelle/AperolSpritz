#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_err.h"

class StepperMotor {
public:
    esp_err_t init();
    esp_err_t start(bool direction, uint32_t speed_steps_s, uint32_t max_steps, uint32_t high_us);
    void stop();
    void emergency_stop_isr();
    void request_stop_isr();
    void clear_abort();
    void clear_stop_latch();
    bool is_running() const { return running_; }
    bool abort_requested() const { return abort_requested_; }
    bool limit_reached() const { return limit_reached_; }
    bool is_enabled() const { return enabled_; }
    bool stop_latched() const { return stop_latched_; }
    uint32_t completed_steps() const { return completed_steps_; }

private:
    static IRAM_ATTR bool on_alarm(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *ctx);
    gptimer_handle_t timer_ = nullptr;
    volatile bool running_ = false;
    volatile bool timer_started_ = false;
    volatile bool enabled_ = false;
    volatile bool abort_requested_ = false;
    volatile bool stop_latched_ = false;
    volatile bool pulse_high_ = false;
    volatile bool limit_reached_ = false;
    volatile uint32_t completed_steps_ = 0;
    volatile uint32_t max_steps_ = 0;
};
