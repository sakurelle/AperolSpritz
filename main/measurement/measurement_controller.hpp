#pragma once

#include "config/device_config.hpp"
#include "gpio/gpio_manager.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

class ConfigStorage;
class StepperMotor;

enum class DeviceState : uint8_t {
    IDLE,
    MEASURE_FAST,
    MEASURE_RETRACT,
    MEASURE_FINE,
    MEASURE_FINAL_RETRACT,
    CALIBRATE_FAST,
    CALIBRATE_RETRACT,
    CALIBRATE_FINE,
    CALIBRATE_FINAL_RETRACT,
    AUTO_WAIT_NEEDLE,
    AUTO_WAIT_REMOVE,
    AUTO_MOVE_TO_LOAD,
    MANUAL_LEFT,
    FINISHED,
    STOPPED,
    ERROR,
};

enum class ErrorCode : uint8_t {
    None,
    NeedleNotFound,
    ContactAlreadyActive,
    ContactReleaseTimeout,
    MeasurementTimeout,
    CalibrationTimeout,
    MaxStepsReached,
    NotCalibrated,
    NeedleInsertedDuringAutocal,
    StopActive,
    InvalidConfig,
    InternalError,
};

enum class CommandType : uint8_t { Measure, Calibrate, AutoStart, AutoStop, ResetStop, ResetError, FactoryReset };
struct ControllerCommand { CommandType type; };

struct StatusSnapshot {
    DeviceState state;
    ErrorCode error;
    bool needle_present;
    bool contact;
    bool stop_active;
    bool motor_enabled;
    bool auto_mode_enabled;
    const char *auto_phase;
    uint32_t current_speed_steps_s;
    int64_t position_steps;
    DeviceStats stats;
    DeviceConfig config;
};

class MeasurementController {
public:
    esp_err_t init(StepperMotor *, GpioManager *, ConfigStorage *, DeviceConfig, DeviceStats);
    esp_err_t start_task();
    bool enqueue(CommandType);
    bool update_config(const DeviceConfig &, const char **);
    bool snapshot(StatusSnapshot &);
    double calculate_length(int64_t measurement_position_steps) const;

private:
    static void task_entry(void *);
    void run();
    void process_gpio(const GpioEvent &);
    void process_command(const ControllerCommand &);
    void tick();
    void start_operation(bool calibration, bool automatic = false);
    bool start_motion(DeviceState state, bool direction_positive, uint32_t speed, uint32_t max_steps, bool stop_on_contact);
    void handle_contact();
    void start_retract(bool final_retract);
    void tick_retract(uint32_t now);
    void evaluate_measurement_needle(uint32_t now);
    uint32_t retract_steps_from_config() const;
    void finish_operation();
    void start_auto_calibration();
    void start_auto_load_position();
    void enter_auto_wait_needle();
    void tick_auto(uint32_t now);
    void set_auto_enabled(bool enabled);
    void fail(ErrorCode);
    void safe_stop();
    bool debounced(uint32_t now, uint32_t &last) const;
    bool is_approach() const;
    bool is_measurement() const;
    bool is_retract() const;
    bool is_auto_calibration() const;

    StepperMotor *motor_ = nullptr;
    GpioManager *gpio_ = nullptr;
    ConfigStorage *storage_ = nullptr;
    QueueHandle_t gpio_queue_ = nullptr;
    QueueHandle_t command_queue_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    DeviceConfig config_{};
    DeviceStats stats_{};
    DeviceState state_ = DeviceState::IDLE;
    DeviceState retract_next_state_ = DeviceState::IDLE;
    ErrorCode error_ = ErrorCode::None;
    uint32_t operation_started_ms_ = 0;
    uint32_t phase_started_ms_ = 0;
    uint32_t last_tick_ms_ = 0;
    uint32_t last_measure_ms_ = 0;
    uint32_t last_calibrate_ms_ = 0;
    uint32_t optical_absence_started_ms_ = 0;
    uint32_t retract_total_steps_ = 0;
    int64_t retract_contact_position_steps_ = 0;
    int64_t retract_target_position_steps_ = 0;
    bool measurement_result_pending_ = false;
    DeviceStats pending_measurement_stats_{};
    bool auto_mode_enabled_ = false;
    bool operation_auto_ = false;
    bool operation_calibration_ = false;
    uint32_t needle_stable_since_ms_ = 0;
    uint32_t auto_load_steps_ = 0;
};

const char *state_name(DeviceState);
const char *error_name(ErrorCode);
