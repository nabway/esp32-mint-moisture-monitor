#pragma once

namespace sensor {

struct LightReading {
    float lux;    // ambient light in lux; meaningless unless valid == true
    bool  valid;  // false if the sensor is absent or the last read failed
};

/**
 * BH1750FVI ambient light sensor over I2C.
 *
 * Deliberately thin for now: no averaging and no state machine, unlike
 * MoistureSensor. Light swings by orders of magnitude within a day, so what
 * counts as "enough light" needs field data before any thresholds are worth
 * writing. This just reports what the sensor sees.
 */
class LightSensor {
public:
    // Starts the I2C bus and the sensor. Returns false if it does not answer,
    // in which case sample() keeps returning valid == false instead of
    // blocking or crashing.
    bool begin();

    // One conversion. Safe to call even when the sensor is missing.
    LightReading sample();

    // Last reading without triggering a new conversion.
    LightReading last() const { return last_reading_; }

    bool isPresent() const { return present_; }

private:
    bool         present_     = false;
    LightReading last_reading_{0.0f, false};
};

}  // namespace sensor
