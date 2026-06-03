#pragma once
#include <cstdint>
#include <cstddef>

namespace sensor {

enum class MoistureState : uint8_t {
    Critical,     // dangerously dry, plant stressed
    WaterNow,     // water today
    WaterSoon,    // water in a day or two
    Ok,           // ideal range
    Overwatered,  // too wet, let it drain
    Unknown       // not enough samples yet
};

struct MoistureReading {
    uint16_t      raw;      // last raw ADC sample
    float         raw_avg;  // moving average of the last N samples
    uint8_t       percent;  // 0..100 derived from calibration curve
    MoistureState state;    // classified state with hysteresis
};

/**
 * Capacitive moisture sensor abstraction.
 *
 * Not thread-safe — lives inside a single FreeRTOS task, sampled at
 * fixed cadence (e.g. 2 Hz via vTaskDelayUntil).
 */
class MoistureSensor {
public:
    explicit MoistureSensor(int adc_pin);

    // Configure ADC once from setup() or task init.
    void begin();

    // Take one sample and update state. Call every ~500 ms.
    MoistureReading sample();

    // Last reading without triggering a new sample.
    MoistureReading last() const { return last_reading_; }

    // Exposed for unit testing.
    static uint8_t rawToPercent(uint16_t raw);

private:
    static constexpr size_t WINDOW_SIZE = 10;

    void          pushSample(uint16_t raw);
    float         windowAverage() const;
    MoistureState classify(uint8_t pct) const;

    int             adc_pin_;
    uint16_t        window_[WINDOW_SIZE];
    size_t          write_idx_;
    size_t          fill_count_;
    MoistureReading last_reading_;
};

const char* toString(MoistureState s);

}  // namespace sensor