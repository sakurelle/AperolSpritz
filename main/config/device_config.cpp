#include "config/device_config.hpp"
#include "config/motion_config.hpp"
#include <cmath>

DeviceConfig default_config() { return DeviceConfig{}; }

bool validate_config(const DeviceConfig &c, const char **reason) {
    const char *error = nullptr;
    if (c.config_version != CONFIG_VERSION) error = "Unsupported configuration version";
    else if (!std::isfinite(c.calibration_length_mm) || c.calibration_length_mm < 0 || c.calibration_length_mm > 1000) error = "Invalid calibration length";
    else if (!std::isfinite(c.auto_calibration_length_mm) || c.auto_calibration_length_mm < 0 || c.auto_calibration_length_mm > 1000) error = "Invalid auto calibration length";
    else if (!std::isfinite(c.nominal_length_mm) || c.nominal_length_mm < 0 || c.nominal_length_mm > 1000) error = "Invalid nominal length";
    else if (!std::isfinite(c.tolerance_mm) || c.tolerance_mm < 0 || c.tolerance_mm > 100) error = "Invalid tolerance";
    else if (!std::isfinite(c.retract_mm) || c.retract_mm < 0.1 || c.retract_mm > motion_config::MAX_RETRACT_MM) error = "Invalid retract distance";
    else if (!std::isfinite(c.mm_per_step) || c.mm_per_step < motion_config::MIN_MM_PER_STEP || c.mm_per_step > motion_config::MAX_MM_PER_STEP) error = "Invalid mm per step";
    if (reason) *reason = error;
    return error == nullptr;
}
