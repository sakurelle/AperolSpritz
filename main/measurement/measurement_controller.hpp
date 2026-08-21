#pragma once
#include "config/device_config.hpp"
#include "gpio/gpio_manager.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
class StepperMotor; class ConfigStorage;
enum class DeviceState : uint8_t { IDLE, MANUAL_LEFT, MEASURING, CALIBRATING, FINISHED, STOPPED, ERROR };
enum class ErrorCode : uint8_t { None, NeedleNotFound, ContactAlreadyActive, MeasurementTimeout, CalibrationTimeout, MaxStepsReached, NotCalibrated, StopActive, InvalidConfig, InternalError };
enum class CommandType : uint8_t { Measure, Calibrate, ResetStop, ResetError, FactoryReset };
struct ControllerCommand { CommandType type; };
struct StatusSnapshot { DeviceState state; ErrorCode error; bool needle_present, contact, stop_active, motor_enabled; DeviceStats stats; DeviceConfig config; };
class MeasurementController {
public:
    esp_err_t init(StepperMotor *, GpioManager *, ConfigStorage *, DeviceConfig, DeviceStats);
    esp_err_t start_task(); bool enqueue(CommandType); bool update_config(const DeviceConfig &, const char **); bool snapshot(StatusSnapshot &); double calculate_length(uint32_t) const;
private:
    static void task_entry(void *); void run(); void process_gpio(const GpioEvent &); void process_command(const ControllerCommand &); void tick(); void start_measurement(bool); void finish_contact(); void fail(ErrorCode); void safe_stop(); bool debounced(uint32_t, uint32_t &) const;
    StepperMotor *motor_ = nullptr; GpioManager *gpio_ = nullptr; ConfigStorage *storage_ = nullptr; QueueHandle_t gpio_queue_ = nullptr, command_queue_ = nullptr; SemaphoreHandle_t mutex_ = nullptr;
    DeviceConfig config_{}; DeviceStats stats_{}; DeviceState state_ = DeviceState::IDLE; ErrorCode error_ = ErrorCode::None; uint32_t started_ms_ = 0, last_measure_ms_ = 0, last_calibrate_ms_ = 0;
};
const char *state_name(DeviceState); const char *error_name(ErrorCode);
