#pragma once

#include <stdint.h>

// Machine-specific values.  They are deliberately not persisted in NVS and
// are changed only by building and flashing firmware.
namespace motion_config {
constexpr double MM_PER_STEP = 0.0003; // Initial measured estimate; verify mechanically after flashing.
constexpr uint32_t COARSE_SPEED_STEPS_S = 1500;
constexpr uint32_t FINE_SPEED_STEPS_S = 500;
constexpr uint32_t RETRACT_SPEED_STEPS_S = 1500;
constexpr uint32_t JOG_SPEED_STEPS_S = 4000;
constexpr uint32_t ACCELERATION_STEPS_S2 = 4000;

constexpr uint32_t MAX_RETRACT_STEPS = 66667;       // 20 mm at MM_PER_STEP.
constexpr uint32_t MAX_MEASUREMENT_STEPS = 250000;  // 75 mm at MM_PER_STEP.
constexpr uint32_t MAX_CALIBRATION_STEPS = 250000;  // 75 mm at MM_PER_STEP.
constexpr uint32_t MAX_AUTO_LOAD_STEPS = 100000;    // 30 mm at MM_PER_STEP.
constexpr uint32_t MEASUREMENT_TIMEOUT_MS = 180000;
constexpr uint32_t CALIBRATION_TIMEOUT_MS = 240000;
constexpr uint32_t CONTACT_RELEASE_TIMEOUT_MS = 10000;
constexpr uint32_t STEP_HIGH_US = 4;
constexpr uint32_t NEEDLE_DEBOUNCE_MS = 100;
constexpr int32_t MEASUREMENT_SIGN = 1;
constexpr double AUTO_LOAD_POSITION_MM = 182.0;
} // namespace motion_config
