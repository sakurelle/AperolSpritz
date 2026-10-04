#pragma once

#include <cmath>
#include <limits>
#include <stdint.h>

// Machine-specific motion limits and speeds. The actual carriage scale lives
// in DeviceConfig so it can be calibrated and persisted in NVS.
namespace motion_config {
constexpr double DEFAULT_MM_PER_STEP = 0.0003127;
constexpr double MIN_MM_PER_STEP = 0.00001;
constexpr double MAX_MM_PER_STEP = 0.01;
constexpr uint32_t COARSE_SPEED_STEPS_S = 1500;
constexpr uint32_t FINE_SPEED_STEPS_S = 500;
constexpr uint32_t RETRACT_SPEED_STEPS_S = 1500;
constexpr uint32_t AUTO_LOAD_SPEED_STEPS_S = 4000;
constexpr uint32_t JOG_SPEED_STEPS_S = 4000;
constexpr uint32_t SERVICE_MOVE_SPEED_STEPS_S = 4000;
constexpr uint32_t ACCELERATION_STEPS_S2 = 4000;

constexpr double MAX_RETRACT_MM = 20.0;
constexpr double MAX_MEASUREMENT_MM = 75.0;
constexpr double MAX_CALIBRATION_MM = 75.0;
constexpr double MAX_AUTO_LOAD_MM = 30.0;
constexpr double MAX_SERVICE_MOVE_MM = 150.0;

// Returns zero for invalid, unrepresentable, or sub-step distances. All
// runtime mm/step conversions must go through these helpers.
inline uint32_t mm_to_steps(double mm, double mm_per_step) {
    const double distance = std::fabs(mm);
    if (!std::isfinite(distance) || !std::isfinite(mm_per_step) || mm_per_step <= 0.0) return 0;
    const double raw_steps = distance / mm_per_step;
    if (!std::isfinite(raw_steps) || raw_steps < 0.5 || raw_steps > static_cast<double>(std::numeric_limits<uint32_t>::max())) return 0;
    return static_cast<uint32_t>(std::llround(raw_steps));
}

inline double steps_to_mm(int64_t steps, double mm_per_step) {
    return std::isfinite(mm_per_step) && mm_per_step > 0.0 ? static_cast<double>(steps) * mm_per_step : 0.0;
}

constexpr uint32_t MEASUREMENT_TIMEOUT_MS = 180000;
constexpr uint32_t CALIBRATION_TIMEOUT_MS = 240000;
constexpr uint32_t CONTACT_RELEASE_TIMEOUT_MS = 10000;
constexpr uint32_t CONTACT_DEBOUNCE_MS = 3;
constexpr uint32_t MOTION_STALL_TIMEOUT_MS = 1000;
constexpr uint32_t STEP_HIGH_US = 4;
constexpr uint32_t NEEDLE_DEBOUNCE_MS = 100;
constexpr double AUTO_LOAD_POSITION_MM = 182.0;
} // namespace motion_config
