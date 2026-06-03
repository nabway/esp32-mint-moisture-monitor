#include <Arduino.h>
#include "config/pins.hpp"
#include "config/calibration.hpp"
#include "sensor/moisture.hpp"

/**
 * @file main.cpp
 * @brief Project 1 — minimal watering alert.
 *
 * Reuses the calibrated MoistureSensor module. Lights the on-board LED
 * when the substrate drops to <= PCT_ALERT_BELOW (50%), with a small
 * hysteresis band so the LED does not flicker around the threshold.
 *
 * This is the practical "it works on the bench" step, before the full
 * FreeRTOS + OLED + RGB build.
 */

static sensor::MoistureSensor moisture_sensor(pins::MOISTURE_ADC);

// Latched LED state, updated with hysteresis (see loop()).
static bool alert_active = false;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) { }

    Serial.printf("# BOOT reason=%d\n", (int)esp_reset_reason());
    Serial.println("t_ms,raw,raw_avg,percent,state,alert");

    pinMode(pins::ONBOARD_LED, OUTPUT);
    digitalWrite(pins::ONBOARD_LED, HIGH);  // start OFF

    moisture_sensor.begin();
}

void loop() {
    const auto r = moisture_sensor.sample();

    // Hysteresis around the 50% alert threshold:
    //   ON  at <= 50%
    //   OFF at >= 53%
    // Between 50% and 53% the previous state is held.
    if (r.percent <= calibration::PCT_ALERT_BELOW) {
        alert_active = true;
    } else if (r.percent >=
               calibration::PCT_ALERT_BELOW + calibration::PCT_HYSTERESIS) {
        alert_active = false;
    }

    digitalWrite(pins::ONBOARD_LED, alert_active ? HIGH : LOW);

    Serial.printf("%lu,%u,%.1f,%u,%s,%d\n",
                  millis(), r.raw, r.raw_avg, r.percent,
                  sensor::toString(r.state), alert_active ? 1 : 0);

    delay(500);
}
