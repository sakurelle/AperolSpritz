#include "measurement/measurement_controller.hpp"

#include "config/config_storage.hpp"
#include "config/motion_config.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "indication/indicator_manager.hpp"
#include "motor/stepper_motor.hpp"

#include <cmath>
#include <cstring>

namespace {
constexpr char TAG[] = "measurement";
constexpr uint32_t OPTICAL_NEEDLE_LOSS_DEBOUNCE_MS = 5;
uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
constexpr double approach_direction_sign(bool measure_dir_inverted) { return measure_dir_inverted ? 1.0 : -1.0; }
constexpr double length_from_positions(double reference_length_mm, int64_t calibration_position_steps, int64_t measurement_position_steps, bool measure_dir_inverted, double mm_per_step) {
    return reference_length_mm + approach_direction_sign(measure_dir_inverted) * static_cast<double>(calibration_position_steps - measurement_position_steps) * mm_per_step;
}
static_assert(length_from_positions(172.0, 100000, 90000, true, 0.000242) > 172.0, "Positive approach must make a longer object longer");
static_assert(length_from_positions(172.0, -100000, -90000, false, 0.000242) > 172.0, "Negative approach must make a longer object longer");
}

const char *state_name(DeviceState state) {
    switch (state) {
    case DeviceState::IDLE: return "IDLE"; case DeviceState::MEASURE_FAST: return "MEASURE_FAST"; case DeviceState::MEASURE_RETRACT: return "MEASURE_RETRACT";
    case DeviceState::MEASURE_FINE: return "MEASURE_FINE"; case DeviceState::MEASURE_FINAL_RETRACT: return "MEASURE_FINAL_RETRACT";
    case DeviceState::CALIBRATE_FAST: return "CALIBRATE_FAST"; case DeviceState::CALIBRATE_RETRACT: return "CALIBRATE_RETRACT";
    case DeviceState::CALIBRATE_FINE: return "CALIBRATE_FINE"; case DeviceState::CALIBRATE_FINAL_RETRACT: return "CALIBRATE_FINAL_RETRACT";
    case DeviceState::AUTO_WAIT_SENSOR_CLEAR: return "AUTO_WAIT_SENSOR_CLEAR"; case DeviceState::AUTO_WAIT_NEEDLE: return "AUTO_WAIT_NEEDLE"; case DeviceState::AUTO_WAIT_REMOVE: return "AUTO_WAIT_REMOVE"; case DeviceState::AUTO_MOVE_TO_LOAD: return "AUTO_MOVE_TO_LOAD";
    case DeviceState::MANUAL_LEFT: return "MANUAL_LEFT"; case DeviceState::FINISHED: return "FINISHED"; case DeviceState::STOPPED: return "STOPPED"; case DeviceState::ERROR: return "ERROR";
    } return "ERROR";
}
const char *error_name(ErrorCode error) {
    switch (error) {
    case ErrorCode::None: return "NONE"; case ErrorCode::NeedleNotFound: return "NEEDLE_NOT_FOUND"; case ErrorCode::ContactAlreadyActive: return "CONTACT_ALREADY_ACTIVE";
    case ErrorCode::ContactReleaseTimeout: return "CONTACT_RELEASE_TIMEOUT"; case ErrorCode::MeasurementTimeout: return "MEASUREMENT_TIMEOUT"; case ErrorCode::CalibrationTimeout: return "CALIBRATION_TIMEOUT";
    case ErrorCode::MaxStepsReached: return "MAX_STEPS_REACHED"; case ErrorCode::NotCalibrated: return "NOT_CALIBRATED"; case ErrorCode::StopActive: return "STOP_ACTIVE";
    case ErrorCode::InvalidConfig: return "INVALID_CONFIG"; case ErrorCode::InternalError: return "INTERNAL_ERROR";
    } return "INTERNAL_ERROR";
}

esp_err_t MeasurementController::init(StepperMotor *motor, GpioManager *gpio, ConfigStorage *storage, IndicatorManager *indicator, DeviceConfig config, DeviceStats stats) {
    if (!motor || !gpio || !storage || !indicator) return ESP_ERR_INVALID_ARG;
    motor_ = motor; gpio_ = gpio; storage_ = storage; indicator_ = indicator; config_ = config; stats_ = stats;
    gpio_queue_ = xQueueCreate(16, sizeof(GpioEvent)); command_queue_ = xQueueCreate(8, sizeof(ControllerCommand)); mutex_ = xSemaphoreCreateMutex();
    if (!gpio_queue_ || !command_queue_ || !mutex_) return ESP_ERR_NO_MEM;
    return gpio_->init(gpio_queue_, motor_);
}
esp_err_t MeasurementController::start_task() { if (!gpio_queue_ || !command_queue_ || !mutex_ || !motor_ || !gpio_ || !storage_) return ESP_ERR_INVALID_STATE; return xTaskCreate(task_entry, "measurement", 4096, this, 10, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM; }
void MeasurementController::task_entry(void *context) { static_cast<MeasurementController *>(context)->run(); }
bool MeasurementController::enqueue(CommandType type) { const ControllerCommand c{type}; return command_queue_ && xQueueSend(command_queue_, &c, 0) == pdTRUE; }

bool MeasurementController::update_config(const DeviceConfig &config, const char **reason) {
    if (!validate_config(config, reason) || !mutex_) return false;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) { if (reason) *reason = "Controller busy"; return false; }
    if (state_ != DeviceState::IDLE) { xSemaphoreGive(mutex_); if (reason) *reason = "Settings can be changed only in IDLE"; return false; }
    const bool invalidate = std::fabs(config.calibration_length_mm - config_.calibration_length_mm) > 1e-12 || std::fabs(config.auto_calibration_length_mm - config_.auto_calibration_length_mm) > 1e-12 || config.measure_dir_inverted != config_.measure_dir_inverted;
    config_ = config;
    if (invalidate) { stats_.calibration_valid = false; stats_.calibration_source = CalibrationSource::None; stats_.calibration_reference_length_mm = 0; stats_.measurements_since_calibration = 0; }
    esp_err_t err = storage_->save_config(config_); if (err == ESP_OK && invalidate) err = storage_->save_stats(stats_); xSemaphoreGive(mutex_);
    if (err != ESP_OK && reason) *reason = "NVS write failed";
    return err == ESP_OK;
}
bool MeasurementController::snapshot(StatusSnapshot &out) {
    if (!mutex_ || !gpio_ || !motor_) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const char *phase = !auto_mode_enabled_ ? "ВЫКЛ" : state_ == DeviceState::AUTO_WAIT_SENSOR_CLEAR ? "Ожидание освобождения датчика" : state_ == DeviceState::AUTO_WAIT_NEEDLE ? "Ожидание иглы" : state_ == DeviceState::AUTO_WAIT_REMOVE ? "Ожидание снятия иглы" : state_ == DeviceState::AUTO_MOVE_TO_LOAD ? "Возврат в загрузочную позицию" : operation_auto_ && operation_calibration_ ? "Автокалибровка" : operation_auto_ ? "Автоматическое измерение" : "Готов";
    out = {state_, error_, gpio_->needle_present(), gpio_->contact_active(), gpio_->stop_active(), motor_->is_enabled(), auto_mode_enabled_, phase, motor_->current_speed_steps_s(), motor_->position_steps(), stats_, config_}; xSemaphoreGive(mutex_); return true;
}
double MeasurementController::calculate_length(int64_t p) const {
    // StepperMotor adds one position step for a positive approach and subtracts
    // one for a negative approach. A longer object must always add to length.
    return length_from_positions(stats_.calibration_reference_length_mm, stats_.calibration_contact_position_steps, p, config_.measure_dir_inverted, motion_config::MM_PER_STEP);
}
bool MeasurementController::debounced(uint32_t now, uint32_t &last) const { if (now - last < motion_config::NEEDLE_DEBOUNCE_MS) return false; last = now; return true; }
bool MeasurementController::is_measurement() const { return state_ == DeviceState::MEASURE_FAST || state_ == DeviceState::MEASURE_RETRACT || state_ == DeviceState::MEASURE_FINE || state_ == DeviceState::MEASURE_FINAL_RETRACT; }
bool MeasurementController::needle_required_for_measurement() const { return state_ == DeviceState::MEASURE_FAST || state_ == DeviceState::MEASURE_RETRACT || state_ == DeviceState::MEASURE_FINE; }
bool MeasurementController::needle_level_stable(bool expected_present, uint32_t now, uint32_t &since_ms) const {
    if (gpio_->needle_present() != expected_present) { since_ms = 0; return false; }
    if (since_ms == 0) { since_ms = now; return false; }
    return now - since_ms >= motion_config::NEEDLE_DEBOUNCE_MS;
}
bool MeasurementController::is_approach() const { return state_ == DeviceState::MEASURE_FAST || state_ == DeviceState::MEASURE_FINE || state_ == DeviceState::CALIBRATE_FAST || state_ == DeviceState::CALIBRATE_FINE; }
bool MeasurementController::is_retract() const { return state_ == DeviceState::MEASURE_RETRACT || state_ == DeviceState::MEASURE_FINAL_RETRACT || state_ == DeviceState::CALIBRATE_RETRACT || state_ == DeviceState::CALIBRATE_FINAL_RETRACT; }
bool MeasurementController::is_motion_state() const { return is_approach() || is_retract() || state_ == DeviceState::AUTO_MOVE_TO_LOAD || state_ == DeviceState::MANUAL_LEFT; }

void MeasurementController::run() { last_tick_ms_ = now_ms(); ESP_LOGI(TAG, "Controller started"); for (;;) { GpioEvent e{}; ControllerCommand c{}; xSemaphoreTake(mutex_, portMAX_DELAY); while (xQueueReceive(gpio_queue_, &e, 0) == pdTRUE) process_gpio(e); while (xQueueReceive(command_queue_, &c, 0) == pdTRUE) process_command(c); tick(); indicator_->set_error(state_ == DeviceState::ERROR); indicator_->set_busy(is_motion_state()); indicator_->tick(now_ms()); xSemaphoreGive(mutex_); vTaskDelay(pdMS_TO_TICKS(1)); } }
void MeasurementController::process_gpio(const GpioEvent &e) {
    const uint32_t now = now_ms();
    if (e.type == GpioEventType::StopActive && e.level_low) { safe_stop(); indicator_->stop_sound(); set_auto_enabled(false); state_ = DeviceState::STOPPED; error_ = ErrorCode::StopActive; return; }
    if (e.type == GpioEventType::NeedleChanged && needle_required_for_measurement()) { evaluate_measurement_needle(now); return; }
    if (e.type == GpioEventType::ContactActive && e.level_low && is_approach()) { begin_contact_candidate(now); return; }
    if (!auto_mode_enabled_) { if (e.type == GpioEventType::MeasurePressed && e.level_low && debounced(now, last_measure_ms_)) start_operation(false); if (e.type == GpioEventType::CalibratePressed && e.level_low && debounced(now, last_calibrate_ms_)) start_operation(true); }
}
void MeasurementController::process_command(const ControllerCommand &c) {
    switch (c.type) {
    case CommandType::Measure: if (!auto_mode_enabled_) start_operation(false); break; case CommandType::Calibrate: if (!auto_mode_enabled_) start_operation(true); break;
    case CommandType::AutoStart:
        if (state_ != DeviceState::STOPPED && state_ != DeviceState::ERROR && (state_ == DeviceState::IDLE || state_ == DeviceState::FINISHED)) {
            const bool needle_present = gpio_->needle_present();
            set_auto_enabled(true);
            needle_present_since_ms_ = 0;
            needle_absent_since_ms_ = 0;
            sensor_clear_since_ms_ = 0;
            state_ = needle_present && stats_.calibration_valid ? DeviceState::AUTO_WAIT_NEEDLE : DeviceState::AUTO_WAIT_REMOVE;
            ESP_LOGI(TAG, "AUTO: enabled, needle=%s, calibration_valid=%d", needle_present ? "present" : "absent", stats_.calibration_valid);
            if (state_ == DeviceState::AUTO_WAIT_REMOVE) ESP_LOGI(TAG, "AUTO: waiting for needle removal");
        }
        break;
    case CommandType::AutoStop: safe_stop(); indicator_->stop_sound(); set_auto_enabled(false); state_ = DeviceState::IDLE; error_ = ErrorCode::None; break;
    case CommandType::Stop: motor_->emergency_stop_isr(); indicator_->stop_sound(); contact_candidate_ = false; set_auto_enabled(false); state_ = DeviceState::STOPPED; error_ = ErrorCode::StopActive; break;
    case CommandType::ResetStop: if (state_ == DeviceState::STOPPED && !gpio_->stop_active()) { motor_->clear_stop_latch(); motor_->clear_abort(); state_ = DeviceState::IDLE; error_ = ErrorCode::None; } break;
    case CommandType::ResetError: if (state_ == DeviceState::ERROR) { motor_->clear_abort(); state_ = DeviceState::IDLE; error_ = ErrorCode::None; } break;
    case CommandType::FactoryReset: if (state_ == DeviceState::IDLE && !auto_mode_enabled_) storage_->factory_reset(config_, stats_); break;
    }
}
bool MeasurementController::start_motion(DeviceState s, bool pos, uint32_t speed, uint32_t max, bool contact) { motor_->clear_abort(); if (motor_->start(pos, speed, motion_config::ACCELERATION_STEPS_S2, max, motion_config::STEP_HIGH_US, contact) != ESP_OK) { fail(ErrorCode::InternalError); return false; } state_ = s; phase_started_ms_ = now_ms(); contact_candidate_ = false; last_motion_progress_steps_ = motor_->completed_steps(); last_motion_progress_ms_ = phase_started_ms_; return true; }
void MeasurementController::start_operation(bool cal, bool automatic) {
    if ((state_ != DeviceState::IDLE && state_ != DeviceState::FINISHED && !automatic) || gpio_->stop_active()) { if (gpio_->stop_active()) { safe_stop(); indicator_->stop_sound(); set_auto_enabled(false); state_ = DeviceState::STOPPED; error_ = ErrorCode::StopActive; } return; }
    if (!cal && (!gpio_->needle_present() || !stats_.calibration_valid)) { fail(!gpio_->needle_present() ? ErrorCode::NeedleNotFound : ErrorCode::NotCalibrated); return; }
    if (gpio_->contact_active()) { fail(ErrorCode::ContactAlreadyActive); return; }
    operation_auto_ = automatic; operation_calibration_ = cal; error_ = ErrorCode::None; optical_absence_started_ms_ = 0; measurement_result_pending_ = false; operation_started_ms_ = now_ms();
    ESP_LOGI(TAG, "%s", automatic ? (cal ? "AUTO: calibration started" : "AUTO: measurement started") : (cal ? "MANUAL: calibration started" : "MANUAL: measurement started"));
    if (start_motion(cal ? DeviceState::CALIBRATE_FAST : DeviceState::MEASURE_FAST, config_.measure_dir_inverted, motion_config::COARSE_SPEED_STEPS_S, cal ? motion_config::MAX_CALIBRATION_STEPS : motion_config::MAX_MEASUREMENT_STEPS, true)) indicator_->beep_start();
}
void MeasurementController::handle_contact() {
    const DeviceState s = state_; const uint32_t steps = motor_->completed_steps();
    ESP_LOGI(TAG, "CONTACT: handle_contact state=%s completed_steps=%lu", state_name(s), static_cast<unsigned long>(steps));
    safe_stop();
    if (s == DeviceState::MEASURE_FAST || s == DeviceState::CALIBRATE_FAST) { start_retract(false); return; }
    if (s == DeviceState::CALIBRATE_FINE) { stats_.calibration_steps = steps; stats_.calibration_contact_position_steps = motor_->position_steps(); stats_.calibration_reference_length_mm = operation_auto_ ? config_.auto_calibration_length_mm : config_.calibration_length_mm; stats_.calibration_source = operation_auto_ ? CalibrationSource::Auto : CalibrationSource::Manual; stats_.calibration_valid = true; stats_.measurements_since_calibration = 0; ++stats_.calibration_count; if (storage_->save_stats(stats_) != ESP_OK) { fail(ErrorCode::InternalError); return; } ESP_LOGI(TAG, "%s: calibration contact=%lld reference=%.3f", operation_auto_ ? "AUTO" : "MANUAL", static_cast<long long>(stats_.calibration_contact_position_steps), stats_.calibration_reference_length_mm); if (operation_auto_) start_auto_load_position(); else start_retract(true); return; }
    if (s == DeviceState::MEASURE_FINE) {
        pending_measurement_stats_ = stats_;
        pending_measurement_stats_.last_measurement_steps = steps;
        pending_measurement_stats_.last_measurement_contact_position_steps = motor_->position_steps();
        const int64_t delta_steps = stats_.calibration_contact_position_steps - pending_measurement_stats_.last_measurement_contact_position_steps;
        const double direction_sign = approach_direction_sign(config_.measure_dir_inverted);
        const double delta_mm = direction_sign * static_cast<double>(delta_steps) * motion_config::MM_PER_STEP;
        pending_measurement_stats_.last_measured_length = stats_.calibration_reference_length_mm + delta_mm;
        ESP_LOGI(TAG, "MEASURE: reference_length_mm=%.6f calibration_position_steps=%lld measurement_position_steps=%lld delta_steps=%lld direction_sign=%.1f mm_per_step=%.9f delta_mm=%.6f calculated_length_mm=%.6f", stats_.calibration_reference_length_mm, static_cast<long long>(stats_.calibration_contact_position_steps), static_cast<long long>(pending_measurement_stats_.last_measurement_contact_position_steps), static_cast<long long>(delta_steps), direction_sign, motion_config::MM_PER_STEP, delta_mm, pending_measurement_stats_.last_measured_length);
        pending_measurement_stats_.last_deviation = pending_measurement_stats_.last_measured_length - config_.nominal_length_mm;
        std::strncpy(pending_measurement_stats_.last_result, std::fabs(pending_measurement_stats_.last_deviation) <= config_.tolerance_mm ? "OK" : (pending_measurement_stats_.last_deviation < 0 ? "TOO_SHORT" : "TOO_LONG"), sizeof(pending_measurement_stats_.last_result));
        pending_measurement_stats_.last_result[sizeof(pending_measurement_stats_.last_result) - 1] = 0;
        ++pending_measurement_stats_.measurements_since_calibration;
        ++pending_measurement_stats_.total_measurements;
        measurement_result_pending_ = true;
        ESP_LOGI(TAG, "MEASURE: fine contact accepted; needle no longer required");
        start_retract(true);
    }
}
void MeasurementController::start_retract(bool final) { const bool cal = operation_calibration_; retract_total_steps_ = retract_steps_from_config(); if (!retract_total_steps_ || retract_total_steps_ > motion_config::MAX_RETRACT_STEPS) { fail(ErrorCode::MaxStepsReached); return; } const bool pos = !config_.measure_dir_inverted; if (final) { retract_next_state_ = DeviceState::FINISHED; state_ = cal ? DeviceState::CALIBRATE_FINAL_RETRACT : DeviceState::MEASURE_FINAL_RETRACT; } else { retract_next_state_ = cal ? DeviceState::CALIBRATE_FINE : DeviceState::MEASURE_FINE; state_ = cal ? DeviceState::CALIBRATE_RETRACT : DeviceState::MEASURE_RETRACT; } start_motion(state_, pos, motion_config::RETRACT_SPEED_STEPS_S, retract_total_steps_, false); }
void MeasurementController::tick_retract(uint32_t now) { if (gpio_->contact_active() && now - phase_started_ms_ >= motion_config::CONTACT_RELEASE_TIMEOUT_MS) { fail(ErrorCode::ContactReleaseTimeout); return; } if (motor_->completed_steps() < retract_total_steps_) return; safe_stop(); if (gpio_->contact_active()) { fail(ErrorCode::ContactReleaseTimeout); return; } if (retract_next_state_ == DeviceState::FINISHED) { finish_operation(); return; } optical_absence_started_ms_ = 0; start_motion(retract_next_state_, config_.measure_dir_inverted, motion_config::FINE_SPEED_STEPS_S, retract_next_state_ == DeviceState::CALIBRATE_FINE ? motion_config::MAX_CALIBRATION_STEPS : motion_config::MAX_MEASUREMENT_STEPS, true); }
void MeasurementController::finish_operation() { if (measurement_result_pending_) { if (storage_->save_stats(pending_measurement_stats_) != ESP_OK) { fail(ErrorCode::InternalError); return; } stats_ = pending_measurement_stats_; measurement_result_pending_ = false; ESP_LOGI(TAG, "MEASURE: result committed length=%.3f", stats_.last_measured_length); } if (operation_auto_ && !operation_calibration_) { state_ = DeviceState::AUTO_WAIT_REMOVE; needle_absent_since_ms_ = 0; indicator_->beep_success(); ESP_LOGI(TAG, "AUTO: entered WAIT_REMOVE, needle=%s", gpio_->needle_present() ? "present" : "absent"); return; } operation_auto_ = false; operation_calibration_ = false; state_ = DeviceState::FINISHED; error_ = ErrorCode::None; indicator_->beep_success(); }
void MeasurementController::start_auto_calibration() { if (!auto_mode_enabled_) return; start_operation(true, true); }
void MeasurementController::start_auto_load_position() { const double delta = motion_config::AUTO_LOAD_POSITION_MM - config_.auto_calibration_length_mm, raw = delta / motion_config::MM_PER_STEP; if (!std::isfinite(delta) || !std::isfinite(raw) || delta <= 0 || delta > motion_config::MAX_AUTO_LOAD_MM || raw <= 0 || raw > motion_config::MAX_AUTO_LOAD_STEPS) { fail(ErrorCode::MaxStepsReached); return; } auto_load_steps_ = static_cast<uint32_t>(std::llround(raw)); ESP_LOGI(TAG, "AUTO: calibration completed"); ESP_LOGI(TAG, "AUTO: moving to load position %.3f mm (%lu steps)", motion_config::AUTO_LOAD_POSITION_MM, static_cast<unsigned long>(auto_load_steps_)); start_motion(DeviceState::AUTO_MOVE_TO_LOAD, !config_.measure_dir_inverted, motion_config::AUTO_LOAD_SPEED_STEPS_S, motion_config::MAX_AUTO_LOAD_STEPS, false); }
void MeasurementController::enter_auto_wait_sensor_clear() { state_ = DeviceState::AUTO_WAIT_SENSOR_CLEAR; operation_auto_ = false; operation_calibration_ = false; sensor_clear_since_ms_ = 0; needle_present_since_ms_ = 0; ESP_LOGI(TAG, "AUTO: waiting for optical sensor clear"); }
void MeasurementController::set_auto_enabled(bool enabled) { auto_mode_enabled_ = enabled; if (!enabled) { operation_auto_ = false; operation_calibration_ = false; } }
void MeasurementController::begin_contact_candidate(uint32_t now) {
    if (contact_candidate_ || !is_approach()) return;
    if (!motor_->contact_abort_requested()) motor_->request_contact_abort_isr();
    contact_candidate_ = true;
    contact_candidate_since_ms_ = now;
    ESP_LOGI(TAG, "CONTACT: falling edge, STEP aborted");
    ESP_LOGI(TAG, "CONTACT: candidate started");
}

bool MeasurementController::tick_contact_candidate(uint32_t now) {
    if (!contact_candidate_) return false;
    const uint32_t elapsed = now - contact_candidate_since_ms_;
    if (!gpio_->contact_active()) {
        contact_candidate_ = false;
        if (motor_->clear_contact_abort()) {
            last_motion_progress_steps_ = motor_->completed_steps();
            last_motion_progress_ms_ = now;
            ESP_LOGI(TAG, "CONTACT: bounce rejected after %lu ms", static_cast<unsigned long>(elapsed));
            ESP_LOGI(TAG, "CONTACT: approach resumed");
            return false;
        }
        ESP_LOGW(TAG, "CONTACT: bounce cannot resume, stop=%d abort=%d limit=%d", motor_->stop_latched(), motor_->abort_requested(), motor_->limit_reached());
        return true;
    }
    if (elapsed < motion_config::CONTACT_DEBOUNCE_MS) return true;
    contact_candidate_ = false;
    ESP_LOGI(TAG, "CONTACT: confirmed after %lu ms", static_cast<unsigned long>(elapsed));
    if (needle_required_for_measurement() && !gpio_->needle_present()) {
        fail(ErrorCode::NeedleNotFound);
        return true;
    }
    handle_contact();
    return true;
}

void MeasurementController::check_motion_stall(uint32_t now) {
    if (!is_motion_state() || contact_candidate_ || gpio_->stop_active() || motor_->limit_reached()) return;
    const uint32_t completed = motor_->completed_steps();
    if (completed != last_motion_progress_steps_) {
        last_motion_progress_steps_ = completed;
        last_motion_progress_ms_ = now;
        return;
    }
    if (now - last_motion_progress_ms_ < motion_config::MOTION_STALL_TIMEOUT_MS) return;
    ESP_LOGE(TAG, "MOTION STALL: state=%s completed_steps=%lu contact=%d needle=%d stop=%d abort=%d contact_abort=%d limit=%d", state_name(state_), static_cast<unsigned long>(completed), gpio_->contact_active(), gpio_->needle_present(), gpio_->stop_active(), motor_->abort_requested(), motor_->contact_abort_requested(), motor_->limit_reached());
    fail(ErrorCode::InternalError);
}
void MeasurementController::tick_auto(uint32_t now) {
    if (!auto_mode_enabled_) return;
    if (state_ == DeviceState::AUTO_WAIT_REMOVE) {
        if (!gpio_->needle_present() && needle_absent_since_ms_ == 0) ESP_LOGI(TAG, "AUTO: needle absent candidate");
        if (needle_level_stable(false, now, needle_absent_since_ms_)) { ESP_LOGI(TAG, "AUTO: needle removed confirmed"); start_auto_calibration(); }
    } else if (state_ == DeviceState::AUTO_MOVE_TO_LOAD) {
        if (gpio_->contact_active() && now - phase_started_ms_ >= motion_config::CONTACT_RELEASE_TIMEOUT_MS) { fail(ErrorCode::ContactReleaseTimeout); return; }
        if (now - phase_started_ms_ >= motion_config::CALIBRATION_TIMEOUT_MS) { fail(ErrorCode::CalibrationTimeout); return; }
        if (motor_->completed_steps() >= auto_load_steps_) { safe_stop(); if (gpio_->contact_active()) { fail(ErrorCode::ContactReleaseTimeout); return; } ESP_LOGI(TAG, "AUTO: load position reached"); enter_auto_wait_sensor_clear(); }
        if (motor_->limit_reached()) { fail(ErrorCode::MaxStepsReached); return; }
    } else if (state_ == DeviceState::AUTO_WAIT_SENSOR_CLEAR) {
        if (gpio_->needle_present()) { sensor_clear_since_ms_ = 0; return; }
        if (needle_level_stable(false, now, sensor_clear_since_ms_)) { state_ = DeviceState::AUTO_WAIT_NEEDLE; needle_present_since_ms_ = 0; indicator_->beep_success(); ESP_LOGI(TAG, "AUTO: optical sensor clear; armed for next needle"); }
    } else if (state_ == DeviceState::AUTO_WAIT_NEEDLE) {
        if (gpio_->needle_present() && needle_present_since_ms_ == 0) ESP_LOGI(TAG, "AUTO: needle present candidate");
        if (needle_level_stable(true, now, needle_present_since_ms_)) { ESP_LOGI(TAG, "AUTO: needle detected confirmed"); start_operation(false, true); }
    }
}
void MeasurementController::safe_stop() { motor_->stop(); }
void MeasurementController::fail(ErrorCode e) { safe_stop(); contact_candidate_ = false; measurement_result_pending_ = false; if (auto_mode_enabled_) set_auto_enabled(false); error_ = e; state_ = DeviceState::ERROR; indicator_->beep_error(); ESP_LOGW(TAG, "%s", error_name(e)); }
uint32_t MeasurementController::retract_steps_from_config() const { const double raw = config_.retract_mm / motion_config::MM_PER_STEP; return std::isfinite(raw) && raw > 0 && raw <= UINT32_MAX ? static_cast<uint32_t>(std::llround(raw)) : 0; }
void MeasurementController::evaluate_measurement_needle(uint32_t now) { if (!needle_required_for_measurement()) return; if (gpio_->needle_present()) { optical_absence_started_ms_ = 0; return; } if (!optical_absence_started_ms_) { optical_absence_started_ms_ = now; return; } if (now - optical_absence_started_ms_ >= OPTICAL_NEEDLE_LOSS_DEBOUNCE_MS) fail(ErrorCode::NeedleNotFound); }
void MeasurementController::tick() {
    const uint32_t now = now_ms(), elapsed = now - last_tick_ms_;
    last_tick_ms_ = now;
    if (gpio_->stop_active() && state_ != DeviceState::STOPPED) {
        motor_->emergency_stop_isr();
        contact_candidate_ = false;
        indicator_->stop_sound();
        set_auto_enabled(false);
        state_ = DeviceState::STOPPED;
        error_ = ErrorCode::StopActive;
        return;
    }
    if (is_approach() && !contact_candidate_ && (motor_->contact_abort_requested() || gpio_->contact_active())) begin_contact_candidate(now);
    if (tick_contact_candidate(now)) return;
    if (needle_required_for_measurement()) {
        evaluate_measurement_needle(now);
        if (state_ == DeviceState::ERROR) return;
    }
    motor_->ramp_tick(elapsed);
    check_motion_stall(now);
    if (state_ == DeviceState::ERROR) return;
    if (is_approach()) {
        if (motor_->limit_reached()) { fail(ErrorCode::MaxStepsReached); return; }
        if (now - operation_started_ms_ >= (is_measurement() ? motion_config::MEASUREMENT_TIMEOUT_MS : motion_config::CALIBRATION_TIMEOUT_MS)) { fail(is_measurement() ? ErrorCode::MeasurementTimeout : ErrorCode::CalibrationTimeout); return; }
    } else if (is_retract()) {
        tick_retract(now);
        return;
    }
    tick_auto(now);
    if (auto_mode_enabled_) return;
    const bool left = gpio_->left_pressed();
    if (state_ == DeviceState::IDLE && left) start_motion(DeviceState::MANUAL_LEFT, !config_.measure_dir_inverted, motion_config::JOG_SPEED_STEPS_S, UINT32_MAX, false);
    if (state_ == DeviceState::MANUAL_LEFT && !left) { safe_stop(); state_ = DeviceState::IDLE; }
    if (state_ == DeviceState::FINISHED) state_ = DeviceState::IDLE;
}
