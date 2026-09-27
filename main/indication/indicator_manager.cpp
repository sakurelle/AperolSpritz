#include "indication/indicator_manager.hpp"

#include "config/indicator_config.hpp"
#include "config/pins.hpp"
#include "driver/gpio.h"
#include "esp_check.h"

esp_err_t IndicatorManager::init() {
    gpio_config_t config{};
    config.pin_bit_mask = (1ULL << BUZZER_PIN) | (1ULL << STATUS_LED_PIN);
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&config), "indicator", "GPIO output setup failed");
    initialized_ = true;
    set_buzzer_logical(false);
    set_led_logical(false);
    return ESP_OK;
}

void IndicatorManager::set_buzzer_logical(bool on) {
    sound_on_ = on;
    gpio_set_level(BUZZER_PIN, indicator_config::BUZZER_INVERTED ? !on : on);
}

void IndicatorManager::set_led_logical(bool on) {
    led_on_ = on;
    gpio_set_level(STATUS_LED_PIN, indicator_config::STATUS_LED_INVERTED ? !on : on);
}

void IndicatorManager::start_single(uint32_t duration_ms) {
    if (!initialized_ || error_) return;
    sound_pattern_ = SoundPattern::Single;
    single_duration_ms_ = duration_ms;
    sound_phase_started_ms_ = 0;
    set_buzzer_logical(true);
}

void IndicatorManager::beep_start() { start_single(indicator_config::BUZZER_START_MS); }

void IndicatorManager::beep_success() { start_single(indicator_config::BUZZER_SUCCESS_MS); }

void IndicatorManager::beep_error() {
    if (!initialized_) return;
    set_error(true);
    sound_pattern_ = SoundPattern::Error;
    error_beeps_remaining_ = indicator_config::BUZZER_ERROR_COUNT;
    sound_phase_started_ms_ = 0;
    set_buzzer_logical(true);
}

void IndicatorManager::set_busy(bool busy) {
    busy_ = busy;
    if (initialized_ && !error_) set_led_logical(busy_);
}

void IndicatorManager::set_error(bool error) {
    if (error_ == error) return;
    error_ = error;
    if (error_) {
        if (sound_pattern_ != SoundPattern::Error) stop_sound();
        led_phase_started_ms_ = 0;
        set_led_logical(true);
    } else {
        stop_sound();
        led_phase_started_ms_ = 0;
        set_led_logical(busy_);
    }
}

void IndicatorManager::stop_sound() {
    sound_pattern_ = SoundPattern::None;
    sound_phase_started_ms_ = 0;
    error_beeps_remaining_ = 0;
    if (initialized_) set_buzzer_logical(false);
}

void IndicatorManager::tick(uint32_t now_ms) {
    if (!initialized_) return;

    if (error_) {
        if (led_phase_started_ms_ == 0) {
            led_phase_started_ms_ = now_ms;
            set_led_logical(true);
        } else if (now_ms - led_phase_started_ms_ >= indicator_config::ERROR_LED_BLINK_MS) {
            led_phase_started_ms_ = now_ms;
            set_led_logical(!led_on_);
        }
    } else {
        set_led_logical(busy_);
    }

    if (sound_pattern_ == SoundPattern::None) return;
    if (sound_phase_started_ms_ == 0) {
        sound_phase_started_ms_ = now_ms;
        return;
    }
    const uint32_t elapsed = now_ms - sound_phase_started_ms_;
    if (sound_pattern_ == SoundPattern::Single) {
        if (elapsed >= single_duration_ms_) stop_sound();
        return;
    }
    if (sound_on_) {
        if (elapsed >= indicator_config::BUZZER_ERROR_ON_MS) {
            set_buzzer_logical(false);
            sound_phase_started_ms_ = now_ms;
            if (error_beeps_remaining_ > 0) --error_beeps_remaining_;
        }
    } else if (elapsed >= indicator_config::BUZZER_ERROR_OFF_MS) {
        if (error_beeps_remaining_ == 0) {
            stop_sound();
        } else {
            set_buzzer_logical(true);
            sound_phase_started_ms_ = now_ms;
        }
    }
}
