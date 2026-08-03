#pragma once
/**
 * @file pins.hpp
 * @brief Single source of truth for all GPIO assignments.
 *
 * Never hardcode pin numbers elsewhere. If a pin must change,
 * this is the only file to touch.
 */

namespace pins {

// ADC1_CH6, input-only, no internal pull-up.
constexpr int MOISTURE_ADC = 34;

// On-board LED of most ESP32 DevKit v1 / WROOM-32 boards (active HIGH).
// Used as a "needs water" alert indicator. If your board's LED sits on a
// different pin, change it here only.
constexpr int ONBOARD_LED = 2;

// I2C bus — BH1750FVI ambient light sensor.
// These are the ESP32 defaults. If the sensor is wired elsewhere, this is the
// only place to change.
constexpr int I2C_SDA = 21;
constexpr int I2C_SCL = 22;

}  // namespace pins
