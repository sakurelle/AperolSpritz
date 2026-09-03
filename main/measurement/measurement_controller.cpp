#include "measurement/measurement_controller.hpp"

#include "config/config_storage.hpp"
#include "config/motion_config.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "motor/stepper_motor.hpp"

#include <cmath>
#include <cstring>

namespace {
constexpr char TAG[] = "measurement";
constexpr uint32_t OPTICAL_NEEDLE_LOSS_DEBOUNCE_MS = 5;
uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
}

const char *state_name(DeviceState state) {
    switch (state) {
    case DeviceState::IDLE: return "IDLE"; case DeviceState::MEASURE_FAST: return "MEASURE_FAST"; case DeviceState::MEASURE_RETRACT: return "MEASURE_RETRACT";
    case DeviceState::MEASURE_FINE: return "MEASURE_FINE"; case DeviceState::MEASURE_FINAL_RETRACT: return "MEASURE_FINAL_RETRACT";
    case DeviceState::CALIBRATE_FAST: return "CALIBRATE_FAST"; case DeviceState::CALIBRATE_RETRACT: return "CALIBRATE_RETRACT";
    case DeviceState::CALIBRATE_FINE: return "CALIBRATE_FINE"; case DeviceState::CALIBRATE_FINAL_RETRACT: return "CALIBRATE_FINAL_RETRACT";
    case DeviceState::AUTO_WAIT_NEEDLE: return "AUTO_WAIT_NEEDLE"; case DeviceState::AUTO_WAIT_REMOVE: return "AUTO_WAIT_REMOVE"; case DeviceState::AUTO_MOVE_TO_LOAD: return "AUTO_MOVE_TO_LOAD";
    case DeviceState::MANUAL_LEFT: return "MANUAL_LEFT"; case DeviceState::FINISHED: return "FINISHED"; case DeviceState::STOPPED: return "STOPPED"; case DeviceState::ERROR: return "ERROR";
    } return "ERROR";
}
const char *error_name(ErrorCode error) {
    switch (error) {
    case ErrorCode::None: return "NONE"; case ErrorCode::NeedleNotFound: return "NEEDLE_NOT_FOUND"; case ErrorCode::ContactAlreadyActive: return "CONTACT_ALREADY_ACTIVE";
    case ErrorCode::ContactReleaseTimeout: return "CONTACT_RELEASE_TIMEOUT"; case ErrorCode::MeasurementTimeout: return "MEASUREMENT_TIMEOUT"; case ErrorCode::CalibrationTimeout: return "CALIBRATION_TIMEOUT";
    case ErrorCode::MaxStepsReached: return "MAX_STEPS_REACHED"; case ErrorCode::NotCalibrated: return "NOT_CALIBRATED"; case ErrorCode::StopActive: return "STOP_ACTIVE";
    case ErrorCode::InvalidConfig: return "INVALID_CONFIG"; case ErrorCode::NeedleInsertedDuringAutocal: return "NEEDLE_INSERTED_DURING_AUTOCAL"; case ErrorCode::InternalError: return "INTERNAL_ERROR";
    } return "INTERNAL_ERROR";
}

esp_err_t MeasurementController::init(StepperMotor *motor, GpioManager *gpio, ConfigStorage *storage, DeviceConfig config, DeviceStats stats) {
    if (!motor || !gpio || !storage) return ESP_ERR_INVALID_ARG;
    motor_ = motor; gpio_ = gpio; storage_ = storage; config_ = config; stats_ = stats;
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
    const char *phase = !auto_mode_enabled_ ? "OFF" : state_ == DeviceState::AUTO_WAIT_NEEDLE ? "AUTO_WAIT_NEEDLE" : state_ == DeviceState::AUTO_WAIT_REMOVE ? "AUTO_WAIT_REMOVE" : state_ == DeviceState::AUTO_MOVE_TO_LOAD ? "AUTO_MOVE_TO_LOAD" : operation_auto_ && operation_calibration_ ? "AUTO_CALIBRATING" : operation_auto_ ? "AUTO_MEASURING" : "AUTO_READY";
    out = {state_, error_, gpio_->needle_present(), gpio_->contact_active(), gpio_->stop_active(), motor_->is_enabled(), auto_mode_enabled_, phase, motor_->current_speed_steps_s(), motor_->position_steps(), stats_, config_}; xSemaphoreGive(mutex_); return true;
}
double MeasurementController::calculate_length(int64_t p) const { return stats_.calibration_reference_length_mm + motion_config::MEASUREMENT_SIGN * static_cast<double>(stats_.calibration_contact_position_steps - p) * motion_config::MM_PER_STEP; }
bool MeasurementController::debounced(uint32_t now, uint32_t &last) const { if (now - last < motion_config::NEEDLE_DEBOUNCE_MS) return false; last = now; return true; }
bool MeasurementController::is_measurement() const { return state_ == DeviceState::MEASURE_FAST || state_ == DeviceState::MEASURE_RETRACT || state_ == DeviceState::MEASURE_FINE || state_ == DeviceState::MEASURE_FINAL_RETRACT; }
bool MeasurementController::is_auto_calibration() const { return operation_auto_ && operation_calibration_ && (state_ == DeviceState::CALIBRATE_FAST || state_ == DeviceState::CALIBRATE_RETRACT || state_ == DeviceState::CALIBRATE_FINE); }
bool MeasurementController::is_approach() const { return state_ == DeviceState::MEASURE_FAST || state_ == DeviceState::MEASURE_FINE || state_ == DeviceState::CALIBRATE_FAST || state_ == DeviceState::CALIBRATE_FINE; }
bool MeasurementController::is_retract() const { return state_ == DeviceState::MEASURE_RETRACT || state_ == DeviceState::MEASURE_FINAL_RETRACT || state_ == DeviceState::CALIBRATE_RETRACT || state_ == DeviceState::CALIBRATE_FINAL_RETRACT; }

void MeasurementController::run() { last_tick_ms_ = now_ms(); ESP_LOGI(TAG, "Controller started"); for (;;) { GpioEvent e{}; ControllerCommand c{}; xSemaphoreTake(mutex_, portMAX_DELAY); while (xQueueReceive(gpio_queue_, &e, 0) == pdTRUE) process_gpio(e); while (xQueueReceive(command_queue_, &c, 0) == pdTRUE) process_command(c); tick(); xSemaphoreGive(mutex_); vTaskDelay(pdMS_TO_TICKS(1)); } }
void MeasurementController::process_gpio(const GpioEvent &e) {
    const uint32_t now = now_ms();
    if (e.type == GpioEventType::StopActive && e.level_low) { safe_stop(); set_auto_enabled(false); state_ = DeviceState::STOPPED; error_ = ErrorCode::StopActive; return; }
    if (e.type == GpioEventType::NeedleChanged && is_auto_calibration() && gpio_->needle_present()) { fail(ErrorCode::NeedleInsertedDuringAutocal); return; }
    if (e.type == GpioEventType::NeedleChanged && is_measurement()) { evaluate_measurement_needle(now); return; }
    if (e.type == GpioEventType::ContactActive && is_approach() && gpio_->contact_active()) { if (is_measurement() && !gpio_->needle_present()) { fail(ErrorCode::NeedleNotFound); return; } handle_contact(); return; }
    if (!auto_mode_enabled_) { if (e.type == GpioEventType::MeasurePressed && e.level_low && debounced(now, last_measure_ms_)) start_operation(false); if (e.type == GpioEventType::CalibratePressed && e.level_low && debounced(now, last_calibrate_ms_)) start_operation(true); }
}
void MeasurementController::process_command(const ControllerCommand &c) {
    switch (c.type) {
    case CommandType::Measure: if (!auto_mode_enabled_) start_operation(false); break; case CommandType::Calibrate: if (!auto_mode_enabled_) start_operation(true); break;
    case CommandType::AutoStart: if (state_ != DeviceState::STOPPED && state_ != DeviceState::ERROR && (state_ == DeviceState::IDLE || state_ == DeviceState::FINISHED)) { set_auto_enabled(true); state_ = gpio_->needle_present() && stats_.calibration_valid ? DeviceState::AUTO_WAIT_NEEDLE : DeviceState::AUTO_WAIT_REMOVE; needle_stable_since_ms_ = now_ms(); ESP_LOGI(TAG, "AUTO: enabled"); } break;
    case CommandType::AutoStop: safe_stop(); set_auto_enabled(false); state_ = DeviceState::IDLE; error_ = ErrorCode::None; break;
    case CommandType::ResetStop: if (state_ == DeviceState::STOPPED && !gpio_->stop_active()) { motor_->clear_stop_latch(); motor_->clear_abort(); state_ = DeviceState::IDLE; error_ = ErrorCode::None; } break;
    case CommandType::ResetError: if (state_ == DeviceState::ERROR) { motor_->clear_abort(); state_ = DeviceState::IDLE; error_ = ErrorCode::None; } break;
    case CommandType::FactoryReset: if (state_ == DeviceState::IDLE && !auto_mode_enabled_) storage_->factory_reset(config_, stats_); break;
    }
}
bool MeasurementController::start_motion(DeviceState s, bool pos, uint32_t speed, uint32_t max, bool contact) { motor_->clear_abort(); if (motor_->start(pos, speed, motion_config::ACCELERATION_STEPS_S2, max, motion_config::STEP_HIGH_US, contact) != ESP_OK) { fail(ErrorCode::InternalError); return false; } state_ = s; phase_started_ms_ = now_ms(); return true; }
void MeasurementController::start_operation(bool cal, bool automatic) {
    if ((state_ != DeviceState::IDLE && state_ != DeviceState::FINISHED && !automatic) || gpio_->stop_active()) { if (gpio_->stop_active()) { safe_stop(); set_auto_enabled(false); state_ = DeviceState::STOPPED; error_ = ErrorCode::StopActive; } return; }
    if (!cal && (!gpio_->needle_present() || !stats_.calibration_valid)) { fail(!gpio_->needle_present() ? ErrorCode::NeedleNotFound : ErrorCode::NotCalibrated); return; }
    if (cal && automatic && gpio_->needle_present()) { fail(ErrorCode::NeedleInsertedDuringAutocal); return; } if (gpio_->contact_active()) { fail(ErrorCode::ContactAlreadyActive); return; }
    operation_auto_ = automatic; operation_calibration_ = cal; error_ = ErrorCode::None; optical_absence_started_ms_ = 0; measurement_result_pending_ = false; operation_started_ms_ = now_ms();
    ESP_LOGI(TAG, "%s", automatic ? (cal ? "AUTO: calibration started" : "AUTO: measurement started") : (cal ? "MANUAL: calibration started" : "MANUAL: measurement started"));
    start_motion(cal ? DeviceState::CALIBRATE_FAST : DeviceState::MEASURE_FAST, config_.measure_dir_inverted, motion_config::COARSE_SPEED_STEPS_S, cal ? motion_config::MAX_CALIBRATION_STEPS : motion_config::MAX_MEASUREMENT_STEPS, true);
}
void MeasurementController::handle_contact() {
    const DeviceState s = state_; const uint32_t steps = motor_->completed_steps(); safe_stop();
    if (s == DeviceState::MEASURE_FAST || s == DeviceState::CALIBRATE_FAST) { start_retract(false); return; }
    if (s == DeviceState::CALIBRATE_FINE) { stats_.calibration_steps = steps; stats_.calibration_contact_position_steps = motor_->position_steps(); stats_.calibration_reference_length_mm = operation_auto_ ? config_.auto_calibration_length_mm : config_.calibration_length_mm; stats_.calibration_source = operation_auto_ ? CalibrationSource::Auto : CalibrationSource::Manual; stats_.calibration_valid = true; stats_.measurements_since_calibration = 0; ++stats_.calibration_count; if (storage_->save_stats(stats_) != ESP_OK) { fail(ErrorCode::InternalError); return; } ESP_LOGI(TAG, "%s: calibration contact=%lld reference=%.3f", operation_auto_ ? "AUTO" : "MANUAL", static_cast<long long>(stats_.calibration_contact_position_steps), stats_.calibration_reference_length_mm); if (operation_auto_) start_auto_load_position(); else start_retract(true); return; }
    if (s == DeviceState::MEASURE_FINE) { pending_measurement_stats_ = stats_; pending_measurement_stats_.last_measurement_steps = steps; pending_measurement_stats_.last_measurement_contact_position_steps = motor_->position_steps(); pending_measurement_stats_.last_measured_length = calculate_length(pending_measurement_stats_.last_measurement_contact_position_steps); pending_measurement_stats_.last_deviation = pending_measurement_stats_.last_measured_length - config_.nominal_length_mm; std::strncpy(pending_measurement_stats_.last_result, std::fabs(pending_measurement_stats_.last_deviation) <= config_.tolerance_mm ? "OK" : (pending_measurement_stats_.last_deviation < 0 ? "TOO_SHORT" : "TOO_LONG"), sizeof(pending_measurement_stats_.last_result)); pending_measurement_stats_.last_result[sizeof(pending_measurement_stats_.last_result) - 1] = 0; ++pending_measurement_stats_.measurements_since_calibration; ++pending_measurement_stats_.total_measurements; measurement_result_pending_ = true; start_retract(true); }
}
void MeasurementController::start_retract(bool final) { const bool cal = operation_calibration_; retract_total_steps_ = retract_steps_from_config(); if (!retract_total_steps_ || retract_total_steps_ > motion_config::MAX_RETRACT_STEPS) { fail(ErrorCode::MaxStepsReached); return; } const bool pos = !config_.measure_dir_inverted; if (final) { retract_next_state_ = DeviceState::FINISHED; state_ = cal ? DeviceState::CALIBRATE_FINAL_RETRACT : DeviceState::MEASURE_FINAL_RETRACT; } else { retract_next_state_ = cal ? DeviceState::CALIBRATE_FINE : DeviceState::MEASURE_FINE; state_ = cal ? DeviceState::CALIBRATE_RETRACT : DeviceState::MEASURE_RETRACT; } start_motion(state_, pos, motion_config::RETRACT_SPEED_STEPS_S, retract_total_steps_, false); }
void MeasurementController::tick_retract(uint32_t now) { if (gpio_->contact_active() && now - phase_started_ms_ >= motion_config::CONTACT_RELEASE_TIMEOUT_MS) { fail(ErrorCode::ContactReleaseTimeout); return; } if (motor_->completed_steps() < retract_total_steps_) return; safe_stop(); if (gpio_->contact_active()) { fail(ErrorCode::ContactReleaseTimeout); return; } if (retract_next_state_ == DeviceState::FINISHED) { finish_operation(); return; } optical_absence_started_ms_ = 0; start_motion(retract_next_state_, config_.measure_dir_inverted, motion_config::FINE_SPEED_STEPS_S, retract_next_state_ == DeviceState::CALIBRATE_FINE ? motion_config::MAX_CALIBRATION_STEPS : motion_config::MAX_MEASUREMENT_STEPS, true); }
void MeasurementController::finish_operation() { if (measurement_result_pending_) { if (!gpio_->needle_present()) { fail(ErrorCode::NeedleNotFound); return; } if (storage_->save_stats(pending_measurement_stats_) != ESP_OK) { fail(ErrorCode::InternalError); return; } stats_ = pending_measurement_stats_; measurement_result_pending_ = false; ESP_LOGI(TAG, "AUTO: measurement completed length=%.3f", stats_.last_measured_length); } if (operation_auto_ && !operation_calibration_) { state_ = DeviceState::AUTO_WAIT_REMOVE; needle_stable_since_ms_ = now_ms(); ESP_LOGI(TAG, "AUTO: waiting for needle removal"); return; } operation_auto_ = false; operation_calibration_ = false; state_ = DeviceState::FINISHED; error_ = ErrorCode::None; }
void MeasurementController::start_auto_calibration() { if (!auto_mode_enabled_ || gpio_->needle_present()) { if (gpio_->needle_present()) fail(ErrorCode::NeedleInsertedDuringAutocal); return; } start_operation(true, true); }
void MeasurementController::start_auto_load_position() { const double delta = motion_config::AUTO_LOAD_POSITION_MM - config_.auto_calibration_length_mm, raw = delta / motion_config::MM_PER_STEP; if (!std::isfinite(delta) || !std::isfinite(raw) || delta <= 0 || delta > 30 || raw <= 0 || raw > motion_config::MAX_AUTO_LOAD_STEPS) { fail(ErrorCode::MaxStepsReached); return; } auto_load_steps_ = static_cast<uint32_t>(std::llround(raw)); ESP_LOGI(TAG, "AUTO: moving to load position %.3f mm (%lu steps)", motion_config::AUTO_LOAD_POSITION_MM, static_cast<unsigned long>(auto_load_steps_)); start_motion(DeviceState::AUTO_MOVE_TO_LOAD, !config_.measure_dir_inverted, motion_config::RETRACT_SPEED_STEPS_S, auto_load_steps_, false); }
void MeasurementController::enter_auto_wait_needle() { state_ = DeviceState::AUTO_WAIT_NEEDLE; operation_auto_ = false; operation_calibration_ = false; needle_stable_since_ms_ = now_ms(); ESP_LOGI(TAG, "AUTO: waiting for next needle"); }
void MeasurementController::set_auto_enabled(bool enabled) { auto_mode_enabled_ = enabled; if (!enabled) { operation_auto_ = false; operation_calibration_ = false; } }
void MeasurementController::tick_auto(uint32_t now) { if (!auto_mode_enabled_) return; if (state_ == DeviceState::AUTO_WAIT_NEEDLE) { if (!gpio_->needle_present()) { needle_stable_since_ms_ = now; return; } if (now - needle_stable_since_ms_ >= motion_config::NEEDLE_DEBOUNCE_MS) { if (stats_.calibration_valid) { ESP_LOGI(TAG, "AUTO: needle detected"); start_operation(false, true); } else { state_ = DeviceState::AUTO_WAIT_REMOVE; needle_stable_since_ms_ = now; ESP_LOGI(TAG, "AUTO: remove needle for calibration"); } } } else if (state_ == DeviceState::AUTO_WAIT_REMOVE) { if (gpio_->needle_present()) { needle_stable_since_ms_ = now; return; } if (now - needle_stable_since_ms_ >= motion_config::NEEDLE_DEBOUNCE_MS) { ESP_LOGI(TAG, "AUTO: needle removed"); start_auto_calibration(); } } else if (state_ == DeviceState::AUTO_MOVE_TO_LOAD) { if (gpio_->needle_present()) { fail(ErrorCode::NeedleInsertedDuringAutocal); return; } if (gpio_->contact_active() && now - phase_started_ms_ >= motion_config::CONTACT_RELEASE_TIMEOUT_MS) { fail(ErrorCode::ContactReleaseTimeout); return; } if (motor_->limit_reached()) { fail(ErrorCode::MaxStepsReached); return; } if (motor_->completed_steps() >= auto_load_steps_) { safe_stop(); if (gpio_->contact_active()) { fail(ErrorCode::ContactReleaseTimeout); return; } ESP_LOGI(TAG, "AUTO: load position reached"); enter_auto_wait_needle(); } } }
void MeasurementController::safe_stop() { motor_->stop(); }
void MeasurementController::fail(ErrorCode e) { safe_stop(); measurement_result_pending_ = false; if (auto_mode_enabled_) set_auto_enabled(false); error_ = e; state_ = DeviceState::ERROR; ESP_LOGW(TAG, "%s", error_name(e)); }
uint32_t MeasurementController::retract_steps_from_config() const { const double raw = config_.retract_mm / motion_config::MM_PER_STEP; return std::isfinite(raw) && raw > 0 && raw <= UINT32_MAX ? static_cast<uint32_t>(std::llround(raw)) : 0; }
void MeasurementController::evaluate_measurement_needle(uint32_t now) { if (!is_measurement()) return; if (gpio_->needle_present()) { optical_absence_started_ms_ = 0; return; } if (!optical_absence_started_ms_) { optical_absence_started_ms_ = now; return; } if (now - optical_absence_started_ms_ >= OPTICAL_NEEDLE_LOSS_DEBOUNCE_MS) fail(ErrorCode::NeedleNotFound); }
void MeasurementController::tick() { const uint32_t now = now_ms(), elapsed = now - last_tick_ms_; last_tick_ms_ = now; if (gpio_->stop_active() && state_ != DeviceState::STOPPED) { motor_->emergency_stop_isr(); set_auto_enabled(false); state_ = DeviceState::STOPPED; error_ = ErrorCode::StopActive; return; } if (is_auto_calibration() && gpio_->needle_present()) { fail(ErrorCode::NeedleInsertedDuringAutocal); return; } if (is_measurement()) { evaluate_measurement_needle(now); if (!is_measurement()) return; } motor_->ramp_tick(elapsed); if (is_approach()) { if (gpio_->contact_active()) { if (is_measurement() && !gpio_->needle_present()) { fail(ErrorCode::NeedleNotFound); return; } handle_contact(); return; } if (motor_->limit_reached()) { fail(ErrorCode::MaxStepsReached); return; } if (now - operation_started_ms_ >= (is_measurement() ? motion_config::MEASUREMENT_TIMEOUT_MS : motion_config::CALIBRATION_TIMEOUT_MS)) { fail(is_measurement() ? ErrorCode::MeasurementTimeout : ErrorCode::CalibrationTimeout); return; } } else if (is_retract()) { tick_retract(now); return; } tick_auto(now); if (auto_mode_enabled_) return; const bool left = gpio_->left_pressed(); if (state_ == DeviceState::IDLE && left) start_motion(DeviceState::MANUAL_LEFT, !config_.measure_dir_inverted, motion_config::JOG_SPEED_STEPS_S, UINT32_MAX, false); if (state_ == DeviceState::MANUAL_LEFT && !left) { safe_stop(); state_ = DeviceState::IDLE; } if (state_ == DeviceState::FINISHED) state_ = DeviceState::IDLE; }
