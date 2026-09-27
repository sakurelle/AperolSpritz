#pragma once

#include "esp_err.h"

#include <stdint.h>

class IndicatorManager {
public:
    esp_err_t init();

    void beep_start();
    void beep_success();
    void beep_error();

    void set_busy(bool busy);
    void set_error(bool error);
    void stop_sound();
    void tick(uint32_t now_ms);

private:
    enum class SoundPattern : uint8_t { None, Single, Error };

    void set_buzzer_logical(bool on);
    void set_led_logical(bool on);
    void start_single(uint32_t duration_ms);

    bool initialized_ = false;
    bool busy_ = false;
    bool error_ = false;
    bool led_on_ = false;
    bool sound_on_ = false;
    SoundPattern sound_pattern_ = SoundPattern::None;
    uint32_t sound_phase_started_ms_ = 0;
    uint32_t single_duration_ms_ = 0;
    uint32_t error_beeps_remaining_ = 0;
    uint32_t led_phase_started_ms_ = 0;
};
