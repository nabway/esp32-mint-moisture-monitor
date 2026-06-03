#include "sensor/moisture.hpp"
#include "config/calibration.hpp"
#include <Arduino.h>

namespace sensor {

MoistureSensor::MoistureSensor(int adc_pin)
    : adc_pin_(adc_pin),
      window_{0},
      write_idx_(0),
      fill_count_(0),
      last_reading_{0, 0.0f, 0, MoistureState::Unknown} {}

void MoistureSensor::begin() {
    analogReadResolution(12);
    analogSetPinAttenuation(adc_pin_, ADC_11db);
}

void MoistureSensor::pushSample(uint16_t raw) {
    window_[write_idx_] = raw;
    write_idx_ = (write_idx_ + 1) % WINDOW_SIZE;
    if (fill_count_ < WINDOW_SIZE) ++fill_count_;
}

float MoistureSensor::windowAverage() const {
    if (fill_count_ == 0) return 0.0f;
    uint32_t acc = 0;
    for (size_t i = 0; i < fill_count_; ++i) acc += window_[i];
    return static_cast<float>(acc) / static_cast<float>(fill_count_);
}

uint8_t MoistureSensor::rawToPercent(uint16_t raw) {
    using namespace calibration;
    if (raw >= RAW_DRY_FLOOR) return 0;
    if (raw <= RAW_WET_FULL)  return 100;
    const int32_t span = static_cast<int32_t>(RAW_DRY_FLOOR) - RAW_WET_FULL;
    const int32_t num  = static_cast<int32_t>(RAW_DRY_FLOOR) - raw;
    return static_cast<uint8_t>((num * 100) / span);
}

MoistureState MoistureSensor::classify(uint8_t pct) const {
    using namespace calibration;
    const MoistureState prev = last_reading_.state;

    // Raw classification
    MoistureState next;
    if      (pct >= PCT_OK_BELOW)           next = MoistureState::Overwatered;
    else if (pct >= PCT_WATER_SOON_BELOW)   next = MoistureState::Ok;
    else if (pct >= PCT_WATER_NOW_BELOW)    next = MoistureState::WaterSoon;
    else if (pct >= PCT_CRITICAL_BELOW)     next = MoistureState::WaterNow;
    else                                    next = MoistureState::Critical;

    if (prev == MoistureState::Unknown) return next;
    if (next == prev) return prev;

    // Hysteresis: require PCT_HYSTERESIS delta before accepting transition
    auto withinBand = [pct](uint8_t lo, uint8_t hi) {
        return pct >= lo && pct < hi;
    };
    switch (prev) {
        case MoistureState::Overwatered:
            if (withinBand(PCT_OK_BELOW - PCT_HYSTERESIS, PCT_OK_BELOW))
                return prev;
            break;
        case MoistureState::Ok:
            if (withinBand(PCT_WATER_SOON_BELOW - PCT_HYSTERESIS,
                           PCT_WATER_SOON_BELOW))
                return prev;
            if (withinBand(PCT_OK_BELOW, PCT_OK_BELOW + PCT_HYSTERESIS))
                return prev;
            break;
        case MoistureState::WaterSoon:
            if (withinBand(PCT_WATER_NOW_BELOW - PCT_HYSTERESIS,
                           PCT_WATER_NOW_BELOW))
                return prev;
            if (withinBand(PCT_WATER_SOON_BELOW,
                           PCT_WATER_SOON_BELOW + PCT_HYSTERESIS))
                return prev;
            break;
        case MoistureState::WaterNow:
            if (withinBand(PCT_CRITICAL_BELOW - PCT_HYSTERESIS,
                           PCT_CRITICAL_BELOW))
                return prev;
            if (withinBand(PCT_WATER_NOW_BELOW,
                           PCT_WATER_NOW_BELOW + PCT_HYSTERESIS))
                return prev;
            break;
        case MoistureState::Critical:
            if (withinBand(PCT_CRITICAL_BELOW,
                           PCT_CRITICAL_BELOW + PCT_HYSTERESIS))
                return prev;
            break;
        default: break;
    }
    return next;
}

MoistureReading MoistureSensor::sample() {
    const uint16_t raw = static_cast<uint16_t>(analogRead(adc_pin_));
    pushSample(raw);
    const float   avg = windowAverage();
    const uint8_t pct = rawToPercent(static_cast<uint16_t>(avg + 0.5f));

    last_reading_.raw     = raw;
    last_reading_.raw_avg = avg;
    last_reading_.percent = pct;
    last_reading_.state   = classify(pct);
    return last_reading_;
}

const char* toString(MoistureState s) {
    switch (s) {
        case MoistureState::Critical:    return "CRITICAL";
        case MoistureState::WaterNow:    return "WATER_NOW";
        case MoistureState::WaterSoon:   return "WATER_SOON";
        case MoistureState::Ok:          return "OK";
        case MoistureState::Overwatered: return "OVERWATERED";
        case MoistureState::Unknown:     return "UNKNOWN";
    }
    return "?";
}

}  // namespace sensor
