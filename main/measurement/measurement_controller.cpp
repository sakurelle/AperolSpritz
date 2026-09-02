#include "measurement/measurement_controller.hpp"

#include "config/config_storage.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "motor/stepper_motor.hpp"

#include <cmath>
#include <cstring>

namespace {
constexpr char TAG[] = "measurement";
constexpr uint32_t OPTICAL_NEEDLE_LOSS_DEBOUNCE_MS = 5;

uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}
}

const char *state_name(DeviceState state) {
    switch (state) {
    case DeviceState::IDLE: return "IDLE";
    case DeviceState::MEASURE_FAST: return "MEASURE_FAST";
    case DeviceState::MEASURE_RETRACT: return "MEASURE_RETRACT";
    case DeviceState::MEASURE_FINE: return "MEASURE_FINE";
    case DeviceState::MEASURE_FINAL_RETRACT: return "MEASURE_FINAL_RETRACT";
    case DeviceState::CALIBRATE_FAST: return "CALIBRATE_FAST";
    case DeviceState::CALIBRATE_RETRACT: return "CALIBRATE_RETRACT";
    case DeviceState::CALIBRATE_FINE: return "CALIBRATE_FINE";
    case DeviceState::CALIBRATE_FINAL_RETRACT: return "CALIBRATE_FINAL_RETRACT";
    case DeviceState::MANUAL_LEFT: return "MANUAL_LEFT";
    case DeviceState::FINISHED: return "FINISHED";
    case DeviceState::STOPPED: return "STOPPED";
    case DeviceState::ERROR: return "ERROR";
    }
    return "ERROR";
}

const char *error_name(ErrorCode error) {
    switch (error) {
    case ErrorCode::None: return "NONE";
    case ErrorCode::NeedleNotFound: return "NEEDLE_NOT_FOUND";
    case ErrorCode::ContactAlreadyActive: return "CONTACT_ALREADY_ACTIVE";
    case ErrorCode::ContactReleaseTimeout: return "CONTACT_RELEASE_TIMEOUT";
    case ErrorCode::MeasurementTimeout: return "MEASUREMENT_TIMEOUT";
    case ErrorCode::CalibrationTimeout: return "CALIBRATION_TIMEOUT";
    case ErrorCode::MaxStepsReached: return "MAX_STEPS_REACHED";
    case ErrorCode::NotCalibrated: return "NOT_CALIBRATED";
    case ErrorCode::StopActive: return "STOP_ACTIVE";
    case ErrorCode::InvalidConfig: return "INVALID_CONFIG";
    case ErrorCode::InternalError: return "INTERNAL_ERROR";
    }
    return "INTERNAL_ERROR";
}

esp_err_t MeasurementController::init(StepperMotor *motor, GpioManager *gpio, ConfigStorage *storage,
                                      DeviceConfig config, DeviceStats stats) {
    if (!motor || !gpio || !storage) {
        ESP_LOGE(TAG, "Null dependency");
        return ESP_ERR_INVALID_ARG;
    }
    motor_ = motor;
    gpio_ = gpio;
    storage_ = storage;
    config_ = config;
    stats_ = stats;
    gpio_queue_ = xQueueCreate(16, sizeof(GpioEvent));
    command_queue_ = xQueueCreate(8, sizeof(ControllerCommand));
    mutex_ = xSemaphoreCreateMutex();
    if (!gpio_queue_ || !command_queue_ || !mutex_) {
        ESP_LOGE(TAG, "FreeRTOS allocation failed: gpio=%p command=%p mutex=%p", gpio_queue_, command_queue_, mutex_);
        return ESP_ERR_NO_MEM;
    }
    return gpio_->init(gpio_queue_, motor_);
}

esp_err_t MeasurementController::start_task() {
    if (!gpio_queue_ || !command_queue_ || !mutex_ || !motor_ || !gpio_ || !storage_) {
        ESP_LOGE(TAG, "Controller is not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(task_entry, "measurement", 4096, this, 10, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Cannot create controller task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void MeasurementController::task_entry(void *context) {
    static_cast<MeasurementController *>(context)->run();
}

bool MeasurementController::enqueue(CommandType type) {
    if (!command_queue_) {
        ESP_LOGE(TAG, "Command queue is null");
        return false;
    }
    const ControllerCommand command{type};
    return xQueueSend(command_queue_, &command, 0) == pdTRUE;
}

bool MeasurementController::update_config(const DeviceConfig &config, const char **reason) {
    if (!validate_config(config, reason)) return false;
    if (!mutex_) {
        if (reason) *reason = "Controller is not initialized";
        return false;
    }
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        if (reason) *reason = "Controller busy";
        return false;
    }
    if (state_ != DeviceState::IDLE) {
        xSemaphoreGive(mutex_);
        if (reason) *reason = "Settings can be changed only in IDLE";
        return false;
    }
    const bool scale_changed = std::fabs(config.calibration_length_mm - config_.calibration_length_mm) > 1e-12 ||
        std::fabs(config.mm_per_step - config_.mm_per_step) > 1e-12 || config.measurement_sign != config_.measurement_sign;
    config_ = config;
    if (scale_changed) {
        stats_.calibration_valid = false;
        stats_.measurements_since_calibration = 0;
    }
    esp_err_t err = storage_->save_config(config_);
    if (err == ESP_OK && scale_changed) err = storage_->save_stats(stats_);
    xSemaphoreGive(mutex_);
    if (err != ESP_OK) {
        if (reason) *reason = "NVS write failed";
        return false;
    }
    return true;
}

bool MeasurementController::snapshot(StatusSnapshot &out) {
    if (!mutex_ || !gpio_ || !motor_) {
        ESP_LOGE(TAG, "Snapshot requested before controller initialization");
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    out = {state_, error_, gpio_->needle_present(), gpio_->contact_active(), gpio_->stop_active(), motor_->is_enabled(),
           motor_->current_speed_steps_s(), motor_->position_steps(), stats_, config_};
    xSemaphoreGive(mutex_);
    return true;
}

double MeasurementController::calculate_length(int64_t measurement_position_steps) const {
    return config_.calibration_length_mm + config_.measurement_sign *
        static_cast<double>(stats_.calibration_contact_position_steps - measurement_position_steps) * config_.mm_per_step;
}

bool MeasurementController::debounced(uint32_t now, uint32_t &last) const {
    if (now - last < config_.debounce_ms) return false;
    last = now;
    return true;
}

bool MeasurementController::is_measurement() const {
    return state_ == DeviceState::MEASURE_FAST || state_ == DeviceState::MEASURE_RETRACT ||
        state_ == DeviceState::MEASURE_FINE || state_ == DeviceState::MEASURE_FINAL_RETRACT;
}

bool MeasurementController::is_approach() const {
    return state_ == DeviceState::MEASURE_FAST || state_ == DeviceState::MEASURE_FINE ||
        state_ == DeviceState::CALIBRATE_FAST || state_ == DeviceState::CALIBRATE_FINE;
}

bool MeasurementController::is_retract() const {
    return state_ == DeviceState::MEASURE_RETRACT || state_ == DeviceState::MEASURE_FINAL_RETRACT ||
        state_ == DeviceState::CALIBRATE_RETRACT || state_ == DeviceState::CALIBRATE_FINAL_RETRACT;
}

void MeasurementController::run() {
    if (!gpio_queue_ || !command_queue_ || !mutex_ || !motor_ || !gpio_ || !storage_) {
        ESP_LOGE(TAG, "Controller task started without required handles");
        if (motor_) motor_->stop();
        vTaskDelete(nullptr);
        return;
    }
    last_tick_ms_ = now_ms();
    ESP_LOGI(TAG, "Controller started");
    for (;;) {
        GpioEvent gpio_event{};
        ControllerCommand command{};
        xSemaphoreTake(mutex_, portMAX_DELAY);
        while (xQueueReceive(gpio_queue_, &gpio_event, 0) == pdTRUE) process_gpio(gpio_event);
        while (xQueueReceive(command_queue_, &command, 0) == pdTRUE) process_command(command);
        tick();
        xSemaphoreGive(mutex_);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void MeasurementController::process_gpio(const GpioEvent &event) {
    const uint32_t now = now_ms();
    if (event.type == GpioEventType::StopActive && event.level_low) {
        safe_stop();
        state_ = DeviceState::STOPPED;
        error_ = ErrorCode::StopActive;
        ESP_LOGW(TAG, "JOG STOP reason=STOP");
        return;
    }
    if (event.type == GpioEventType::NeedleChanged && is_measurement()) {
        evaluate_measurement_needle(now);
        return;
    }
    if (event.type == GpioEventType::ContactActive && is_approach() && gpio_->contact_active()) {
        // Read the independent optical sensor at CONTACT; queue order cannot make
        // a missing needle look like a successful contact.
        if (is_measurement() && !gpio_->needle_present()) {
            ESP_LOGW(TAG, "MEASURE: optical needle sensor absent at contact");
            fail(ErrorCode::NeedleNotFound);
            return;
        }
        handle_contact();
        return;
    }
    if (event.type == GpioEventType::MeasurePressed && event.level_low && debounced(now, last_measure_ms_)) start_operation(false);
    if (event.type == GpioEventType::CalibratePressed && event.level_low && debounced(now, last_calibrate_ms_)) start_operation(true);
}

void MeasurementController::process_command(const ControllerCommand &command) {
    switch (command.type) {
    case CommandType::Measure: start_operation(false); break;
    case CommandType::Calibrate: start_operation(true); break;
    case CommandType::ResetStop:
        if (state_ == DeviceState::STOPPED && !gpio_->stop_active()) {
            motor_->clear_stop_latch();
            motor_->clear_abort();
            state_ = DeviceState::IDLE;
            error_ = ErrorCode::None;
        }
        break;
    case CommandType::ResetError:
        if (state_ == DeviceState::ERROR) {
            motor_->clear_abort();
            state_ = DeviceState::IDLE;
            error_ = ErrorCode::None;
        }
        break;
    case CommandType::FactoryReset:
        if (state_ == DeviceState::IDLE) storage_->factory_reset(config_, stats_);
        break;
    }
}

bool MeasurementController::start_motion(DeviceState next_state, bool direction_positive, uint32_t speed,
                                         uint32_t max_steps, bool stop_on_contact) {
    motor_->clear_abort();
    if (motor_->start(direction_positive, speed, config_.acceleration_steps_s2, max_steps, config_.step_high_us, stop_on_contact) != ESP_OK) {
        fail(ErrorCode::InternalError);
        return false;
    }
    state_ = next_state;
    phase_started_ms_ = now_ms();
    return true;
}

void MeasurementController::start_operation(bool calibration) {
    if (state_ != DeviceState::IDLE && state_ != DeviceState::FINISHED) return;
    if (gpio_->stop_active()) {
        safe_stop();
        state_ = DeviceState::STOPPED;
        error_ = ErrorCode::StopActive;
        return;
    }
    if (!calibration && !gpio_->needle_present()) {
        fail(ErrorCode::NeedleNotFound);
        return;
    }
    if (!calibration && !stats_.calibration_valid) {
        fail(ErrorCode::NotCalibrated);
        return;
    }
    if (gpio_->contact_active()) {
        fail(ErrorCode::ContactAlreadyActive);
        return;
    }
    error_ = ErrorCode::None;
    optical_absence_started_ms_ = 0;
    measurement_result_pending_ = false;
    operation_started_ms_ = now_ms();
    start_motion(calibration ? DeviceState::CALIBRATE_FAST : DeviceState::MEASURE_FAST, config_.measure_dir_inverted,
                 config_.coarse_speed_steps_s, calibration ? config_.max_calibration_steps : config_.max_measurement_steps, true);
}

void MeasurementController::handle_contact() {
    if (!is_approach()) return;
    const DeviceState contact_state = state_;
    const uint32_t fine_steps = motor_->completed_steps();
    safe_stop();
    ESP_LOGI(TAG, "MEASURE: contact position=%lld state=%s steps=%lu",
             static_cast<long long>(motor_->position_steps()), state_name(contact_state), static_cast<unsigned long>(fine_steps));

    if (contact_state == DeviceState::MEASURE_FAST || contact_state == DeviceState::CALIBRATE_FAST) {
        start_retract(false);
        return;
    }
    if (contact_state == DeviceState::CALIBRATE_FINE) {
        stats_.calibration_steps = fine_steps;
        stats_.calibration_contact_position_steps = motor_->position_steps();
        stats_.calibration_valid = true;
        stats_.measurements_since_calibration = 0;
        ++stats_.calibration_count;
        storage_->save_stats(stats_);
        start_retract(true);
        return;
    }
    if (contact_state == DeviceState::MEASURE_FINE) {
        // Commit measurement statistics only after FINAL_RETRACT. A lost optical
        // needle during that required phase must not become a successful sample.
        pending_measurement_stats_ = stats_;
        pending_measurement_stats_.last_measurement_steps = fine_steps;
        pending_measurement_stats_.last_measurement_contact_position_steps = motor_->position_steps();
        pending_measurement_stats_.last_measured_length = calculate_length(pending_measurement_stats_.last_measurement_contact_position_steps);
        pending_measurement_stats_.last_deviation = pending_measurement_stats_.last_measured_length - config_.nominal_length_mm;
        std::strncpy(pending_measurement_stats_.last_result,
                     std::fabs(pending_measurement_stats_.last_deviation) <= config_.tolerance_mm ? "OK" :
                     (pending_measurement_stats_.last_deviation < 0 ? "TOO_SHORT" : "TOO_LONG"),
                     sizeof(pending_measurement_stats_.last_result));
        pending_measurement_stats_.last_result[sizeof(pending_measurement_stats_.last_result) - 1] = 0;
        ++pending_measurement_stats_.measurements_since_calibration;
        ++pending_measurement_stats_.total_measurements;
        measurement_result_pending_ = true;
        start_retract(true);
    }
}

void MeasurementController::start_retract(bool final_retract) {
    const bool calibration = state_ == DeviceState::CALIBRATE_FAST || state_ == DeviceState::CALIBRATE_FINE;
    retract_total_steps_ = retract_steps_from_config();
    if (retract_total_steps_ == 0 || retract_total_steps_ > config_.max_retract_steps) {
        fail(ErrorCode::MaxStepsReached);
        return;
    }
    retract_contact_position_steps_ = motor_->position_steps();
    const bool retract_direction_positive = !config_.measure_dir_inverted;
    retract_target_position_steps_ = retract_contact_position_steps_ + (retract_direction_positive ? retract_total_steps_ : -static_cast<int64_t>(retract_total_steps_));
    if (final_retract) {
        retract_next_state_ = DeviceState::FINISHED;
        state_ = calibration ? DeviceState::CALIBRATE_FINAL_RETRACT : DeviceState::MEASURE_FINAL_RETRACT;
    } else {
        retract_next_state_ = calibration ? DeviceState::CALIBRATE_FINE : DeviceState::MEASURE_FINE;
        state_ = calibration ? DeviceState::CALIBRATE_RETRACT : DeviceState::MEASURE_RETRACT;
    }
    if (!start_motion(state_, retract_direction_positive, config_.retract_speed_steps_s, retract_total_steps_, false)) return;
    ESP_LOGI(TAG, "MEASURE: retract=%.3f mm (%lu steps), target=%lld", config_.retract_mm,
             static_cast<unsigned long>(retract_total_steps_), static_cast<long long>(retract_target_position_steps_));
}

void MeasurementController::tick_retract(uint32_t now) {
    if (gpio_->contact_active() && now - phase_started_ms_ >= config_.contact_release_timeout_ms) {
        fail(ErrorCode::ContactReleaseTimeout);
        return;
    }
    if (motor_->completed_steps() < retract_total_steps_) return;
    safe_stop();
    if (gpio_->contact_active()) {
        fail(ErrorCode::ContactReleaseTimeout);
        return;
    }
    ESP_LOGI(TAG, "MEASURE: retract completed at position=%lld", static_cast<long long>(motor_->position_steps()));
    if (retract_next_state_ == DeviceState::FINISHED) {
        finish_operation();
        return;
    }
    optical_absence_started_ms_ = 0;
    const uint32_t limit = retract_next_state_ == DeviceState::CALIBRATE_FINE ? config_.max_calibration_steps : config_.max_measurement_steps;
    start_motion(retract_next_state_, config_.measure_dir_inverted, config_.fine_speed_steps_s, limit, true);
}

void MeasurementController::finish_operation() {
    if (measurement_result_pending_) {
        if (!gpio_->needle_present()) {
            ESP_LOGW(TAG, "MEASURE: optical needle sensor absent before success");
            fail(ErrorCode::NeedleNotFound);
            return;
        }
        if (storage_->save_stats(pending_measurement_stats_) != ESP_OK) {
            fail(ErrorCode::InternalError);
            return;
        }
        stats_ = pending_measurement_stats_;
        measurement_result_pending_ = false;
    }
    state_ = DeviceState::FINISHED;
    error_ = ErrorCode::None;
    ESP_LOGI(TAG, "FINAL RETRACT complete; sample released");
}

void MeasurementController::safe_stop() { motor_->stop(); }

void MeasurementController::fail(ErrorCode error) {
    safe_stop();
    measurement_result_pending_ = false;
    error_ = error;
    state_ = DeviceState::ERROR;
    ESP_LOGW(TAG, "%s", error_name(error));
}

uint32_t MeasurementController::retract_steps_from_config() const {
    if (!std::isfinite(config_.mm_per_step) || config_.mm_per_step <= 0.0 ||
        !std::isfinite(config_.retract_mm) || config_.retract_mm <= 0.0) return 0;
    const double raw_steps = config_.retract_mm / config_.mm_per_step;
    if (!std::isfinite(raw_steps) || raw_steps > static_cast<double>(UINT32_MAX)) return 0;
    const long long rounded_steps = std::llround(raw_steps);
    return rounded_steps > 0 && rounded_steps <= static_cast<long long>(UINT32_MAX)
        ? static_cast<uint32_t>(rounded_steps) : 0;
}

void MeasurementController::evaluate_measurement_needle(uint32_t now) {
    if (!is_measurement()) return;
    if (gpio_->needle_present()) {
        optical_absence_started_ms_ = 0;
        return;
    }
    if (optical_absence_started_ms_ == 0) {
        optical_absence_started_ms_ = now;
        return;
    }
    if (now - optical_absence_started_ms_ >= OPTICAL_NEEDLE_LOSS_DEBOUNCE_MS) {
        ESP_LOGW(TAG, "MEASURE: optical needle sensor confirmed absent");
        fail(ErrorCode::NeedleNotFound);
    }
}

void MeasurementController::tick() {
    const uint32_t now = now_ms();
    const uint32_t elapsed = now - last_tick_ms_;
    last_tick_ms_ = now;
    if (gpio_->stop_active() && state_ != DeviceState::STOPPED) {
        motor_->emergency_stop_isr();
        state_ = DeviceState::STOPPED;
        error_ = ErrorCode::StopActive;
        return;
    }
    if (is_measurement()) {
        evaluate_measurement_needle(now);
        if (!is_measurement()) return;
    }
    motor_->ramp_tick(elapsed);

    if (is_approach()) {
        if (gpio_->contact_active()) {
            if (is_measurement() && !gpio_->needle_present()) {
                ESP_LOGW(TAG, "MEASURE: optical needle sensor absent at contact");
                fail(ErrorCode::NeedleNotFound);
                return;
            }
            handle_contact();
            return;
        }
        if (motor_->limit_reached()) {
            fail(ErrorCode::MaxStepsReached);
            return;
        }
        const uint32_t timeout = is_measurement() ? config_.measurement_timeout_ms : config_.calibration_timeout_ms;
        if (now - operation_started_ms_ >= timeout) {
            fail(is_measurement() ? ErrorCode::MeasurementTimeout : ErrorCode::CalibrationTimeout);
            return;
        }
    } else if (is_retract()) {
        tick_retract(now);
        return;
    }

    const bool left = gpio_->left_pressed();
    if (state_ == DeviceState::IDLE && left) {
        if (start_motion(DeviceState::MANUAL_LEFT, config_.left_dir_inverted, config_.manual_speed_steps_s, UINT32_MAX, false)) {
            ESP_LOGI(TAG, "JOG START direction=%s speed=%lu contact=%d", config_.left_dir_inverted ? "HIGH" : "LOW",
                     static_cast<unsigned long>(config_.manual_speed_steps_s), gpio_->contact_active());
        }
    }
    if (state_ == DeviceState::MANUAL_LEFT && !left) {
        safe_stop();
        state_ = DeviceState::IDLE;
        ESP_LOGI(TAG, "JOG STOP reason=BUTTON_RELEASE");
    }
    if (state_ == DeviceState::FINISHED) state_ = DeviceState::IDLE;
}
