#include "config/device_config.hpp"
#include <cmath>

DeviceConfig default_config() { return DeviceConfig{}; }

bool validate_config(const DeviceConfig &c, const char **reason) {
    const char *error = nullptr;
    if (c.config_version != CONFIG_VERSION) error = "Unsupported configuration version";
    else if (!std::isfinite(c.mm_per_step) || c.mm_per_step <= 0.0 || c.mm_per_step > 1.0) error = "mm_per_step must be between 0 and 1";
    else if (!std::isfinite(c.calibration_length_mm) || c.calibration_length_mm < 0 || c.calibration_length_mm > 1000) error = "Invalid calibration length";
    else if (!std::isfinite(c.nominal_length_mm) || c.nominal_length_mm < 0 || c.nominal_length_mm > 1000) error = "Invalid nominal length";
    else if (!std::isfinite(c.tolerance_mm) || c.tolerance_mm < 0 || c.tolerance_mm > 100) error = "Invalid tolerance";
    else if (c.coarse_speed_steps_s < 100 || c.coarse_speed_steps_s > 100000 || c.fine_speed_steps_s < 50 || c.fine_speed_steps_s > 100000 || c.retract_speed_steps_s < 50 || c.retract_speed_steps_s > 100000 || c.manual_speed_steps_s < 50 || c.manual_speed_steps_s > 100000) error = "Invalid motor speed";
    else if (c.acceleration_steps_s2 < 100 || c.acceleration_steps_s2 > 1000000) error = "Invalid acceleration";
    else if (!std::isfinite(c.retract_mm) || c.retract_mm < 0.1 || c.retract_mm > 20.0) error = "retract_mm must be between 0.1 and 20";
    else if (c.max_retract_steps == 0 || c.max_retract_steps > 10000000 || c.max_measurement_steps == 0 || c.max_calibration_steps == 0 || c.max_measurement_steps > 10000000 || c.max_calibration_steps > 10000000) error = "Invalid step limit";
    else if (c.retract_mm / c.mm_per_step > static_cast<double>(c.max_retract_steps) + 0.5) error = "Retract distance exceeds step safety limit";
    else if (c.measurement_timeout_ms < 100 || c.calibration_timeout_ms < 100 || c.contact_release_timeout_ms < 100 || c.measurement_timeout_ms > 3600000 || c.calibration_timeout_ms > 3600000 || c.contact_release_timeout_ms > 3600000) error = "Invalid timeout";
    else if (c.step_high_us < 2 || c.step_high_us > 1000) error = "Invalid STEP high time";
    else if (c.measurement_sign != 1 && c.measurement_sign != -1) error = "measurement_sign must be +1 or -1";
    else if (c.debounce_ms > 1000) error = "Invalid debounce";
    if (reason) *reason = error;
    return error == nullptr;
}
