#pragma once
#include <cstdint>

/**
 * @file calibration.hpp
 * @brief Moisture sensor calibration points and state thresholds.
 *
 * Values from calibration run documented in docs/calibration_log.md
 * (2026-04-24).
 *
 * Hardware: capacitive soil moisture sensor v1.2, 3.3 V, GPIO34,
 *           12-bit resolution, 11 dB attenuation.
 *
 * Higher raw ADC = drier substrate (inversely proportional to
 * dielectric constant).
 */

namespace calibration {

constexpr uint16_t RAW_DRY_FLOOR = 2600;  // operational dry baseline
constexpr uint16_t RAW_WET_FULL  = 1000;  // freshly watered mint pot

// State thresholds in percent (0 = dry, 100 = saturated).
constexpr uint8_t PCT_CRITICAL_BELOW   = 20;
constexpr uint8_t PCT_WATER_NOW_BELOW  = 40;
constexpr uint8_t PCT_WATER_SOON_BELOW = 60;
constexpr uint8_t PCT_OK_BELOW         = 80;  // >= 80% = overwatered

// Hysteresis applied when transitioning OUT of a state.
constexpr uint8_t PCT_HYSTERESIS = 3;

// Simple LED alert threshold (independent of the 5-state machine above).
// LED turns ON  when percent <= PCT_ALERT_BELOW.
// LED turns OFF when percent >= PCT_ALERT_BELOW + PCT_HYSTERESIS.
// At 50% the linear curve maps to raw ~2100.
constexpr uint8_t PCT_ALERT_BELOW = 50;

}  // namespace calibration
