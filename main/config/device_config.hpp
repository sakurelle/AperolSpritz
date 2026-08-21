#pragma once
#include <stdint.h>

constexpr uint32_t CONFIG_VERSION = 1;

struct DeviceConfig {
    uint32_t config_version = CONFIG_VERSION;
    double mm_per_step = 0.0025;
    double calibration_length_mm = 41.300;
    double nominal_length_mm = 41.300;
    double tolerance_mm = 0.050;
    uint32_t measurement_speed_steps_s = 800;
    uint32_t calibration_speed_steps_s = 400;
    uint32_t manual_speed_steps_s = 300;
    uint32_t max_measurement_steps = 30000;
    uint32_t max_calibration_steps = 30000;
    uint32_t measurement_timeout_ms = 60000;
    uint32_t calibration_timeout_ms = 90000;
    uint32_t step_high_us = 4;
    int32_t measurement_sign = 1;
    bool measure_dir_inverted = false;
    bool left_dir_inverted = true;
    bool disable_motor_when_idle = true;
    bool stop_manual_on_contact = true;
    uint32_t debounce_ms = 50;
};

struct DeviceStats {
    double last_measured_length = 0.0;
    double last_deviation = 0.0;
    uint32_t measurements_since_calibration = 0;
    uint32_t total_measurements = 0;
    uint32_t calibration_count = 0;
    uint32_t last_measurement_steps = 0;
    uint32_t calibration_steps = 0;
    bool calibration_valid = false;
    char last_result[12] = "NONE";
};

DeviceConfig default_config();
bool validate_config(const DeviceConfig &config, const char **reason = nullptr);
