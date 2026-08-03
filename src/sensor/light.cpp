#include "sensor/light.hpp"
#include "config/pins.hpp"
#include <Arduino.h>
#include <Wire.h>
#include <BH1750.h>

namespace sensor {

static BH1750 bh1750;

bool LightSensor::begin() {
    Wire.begin(pins::I2C_SDA, pins::I2C_SCL);

    present_ = bh1750.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

    if (present_) {
        log_i("BH1750 ready on I2C (SDA %d, SCL %d)", pins::I2C_SDA, pins::I2C_SCL);
    } else {
        // Non-fatal: the moisture path keeps working without it.
        log_e("BH1750 not found on I2C (SDA %d, SCL %d) — check wiring and ADDR",
              pins::I2C_SDA, pins::I2C_SCL);
    }

    return present_;
}

LightReading LightSensor::sample() {
    if (!present_) {
        last_reading_ = {0.0f, false};
        return last_reading_;
    }

    // The library returns a negative value on I2C error.
    const float lux = bh1750.readLightLevel();

    if (lux < 0.0f) {
        log_w("BH1750 read failed (%.0f)", lux);
        last_reading_.valid = false;
        return last_reading_;
    }

    last_reading_ = {lux, true};
    return last_reading_;
}

}  // namespace sensor
