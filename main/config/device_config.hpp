#pragma once
#include "config/motion_config.hpp"

#include <stdint.h>

constexpr uint32_t CONFIG_VERSION = 5;

struct DeviceConfig {
    uint32_t config_version = CONFIG_VERSION;
    double calibration_length_mm = 41.300;
    double auto_calibration_length_mm = 170.0;
    double nominal_length_mm = 41.300;
    double tolerance_mm = 0.050;
    double retract_mm = 2.0;
    bool measure_dir_inverted = false;
    double mm_per_step = motion_config::DEFAULT_MM_PER_STEP;
};

enum class CalibrationSource : uint8_t { None, Manual, Auto };

struct DeviceStats {
    double last_measured_length = 0.0;
    double last_deviation = 0.0;
    uint32_t measurements_since_calibration = 0;
    uint32_t total_measurements = 0;
    uint32_t calibration_count = 0;
    uint32_t last_measurement_steps = 0;
    uint32_t calibration_steps = 0;
    int64_t calibration_contact_position_steps = 0;
    int64_t last_measurement_contact_position_steps = 0;
    double calibration_reference_length_mm = 0.0;
    CalibrationSource calibration_source = CalibrationSource::None;
    bool calibration_valid = false;
    char last_result[12] = "NONE";
};

DeviceConfig default_config();
bool validate_config(const DeviceConfig &config, const char **reason = nullptr);
