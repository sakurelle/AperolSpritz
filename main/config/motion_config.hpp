#pragma once

#include <stdint.h>

// Machine-specific values.  They are deliberately not persisted in NVS and
// are changed only by building and flashing firmware.
namespace motion_config {
// Preliminary value derived from 172.000 mm reference, 179.740 mm known
// needle, and 162.401 mm old reported result. Verify with more samples.
constexpr double MM_PER_STEP = 0.0003127;
constexpr uint32_t COARSE_SPEED_STEPS_S = 1500;
constexpr uint32_t FINE_SPEED_STEPS_S = 500;
constexpr uint32_t RETRACT_SPEED_STEPS_S = 1500;
constexpr uint32_t AUTO_LOAD_SPEED_STEPS_S = 4000;
constexpr uint32_t JOG_SPEED_STEPS_S = 4000;
constexpr uint32_t ACCELERATION_STEPS_S2 = 4000;

constexpr double MAX_RETRACT_MM = 20.0;
constexpr double MAX_MEASUREMENT_MM = 75.0;
constexpr double MAX_CALIBRATION_MM = 75.0;
constexpr double MAX_AUTO_LOAD_MM = 30.0;
constexpr uint32_t mm_to_steps(double mm) { return static_cast<uint32_t>(mm / MM_PER_STEP + 0.5); }
constexpr uint32_t MAX_RETRACT_STEPS = mm_to_steps(MAX_RETRACT_MM);
constexpr uint32_t MAX_MEASUREMENT_STEPS = mm_to_steps(MAX_MEASUREMENT_MM);
constexpr uint32_t MAX_CALIBRATION_STEPS = mm_to_steps(MAX_CALIBRATION_MM);
constexpr uint32_t MAX_AUTO_LOAD_STEPS = mm_to_steps(MAX_AUTO_LOAD_MM);
constexpr uint32_t MEASUREMENT_TIMEOUT_MS = 180000;
constexpr uint32_t CALIBRATION_TIMEOUT_MS = 240000;
constexpr uint32_t CONTACT_RELEASE_TIMEOUT_MS = 10000;
constexpr uint32_t STEP_HIGH_US = 4;
constexpr uint32_t NEEDLE_DEBOUNCE_MS = 100;
constexpr double AUTO_LOAD_POSITION_MM = 182.0;
} // namespace motion_config
