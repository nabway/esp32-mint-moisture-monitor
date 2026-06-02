/**
 * @file main.cpp
 * @brief Moisture sensor characterization sketch — Project 1, Phase 2.
 *
 * Purpose: capture raw ADC + millivolts from a capacitive soil moisture
 * sensor so we can define calibration points for mint substrate.
 *
 * This is NOT the final firmware. It has no FreeRTOS tasks, no display,
 * no filtering beyond a simple moving average. It exists only to
 * generate the numbers that will populate config/calibration.hpp later.
 *
 * Protocol for the user:
 *   1) Hold sensor in air for 60 s       → record "DRY_AIR" values.
 *   2) Submerge sensor in a glass of tap water up to the white line
 *      for 60 s                          → record "WET_WATER" values.
 *   3) Stick sensor into the mint pot immediately after watering,
 *      wait 60 s                         → record "WET_SOIL" values.
 *   4) Leave sensor in pot for 48 h and log the drying curve.
 */

#include <Arduino.h>

// -------- Pin configuration --------------------------------------------
static constexpr int PIN_MOISTURE = 34;   // ADC1_CH6, input-only

// -------- Sampling configuration ---------------------------------------
static constexpr uint32_t SAMPLE_PERIOD_MS = 500;   // 2 Hz is plenty
static constexpr size_t   WINDOW_SIZE      = 10;    // moving average N

// -------- State --------------------------------------------------------
static uint16_t window_raw[WINDOW_SIZE] = {0};
static size_t   window_idx = 0;
static bool     window_full = false;

// -------- Helpers ------------------------------------------------------
static float movingAverage(uint16_t new_sample) {
    window_raw[window_idx] = new_sample;
    window_idx = (window_idx + 1) % WINDOW_SIZE;
    if (window_idx == 0) window_full = true;

    const size_t n = window_full ? WINDOW_SIZE : window_idx;
    uint32_t acc = 0;
    for (size_t i = 0; i < n; ++i) acc += window_raw[i];
    return static_cast<float>(acc) / static_cast<float>(n);
}

static void computeMinMax(uint16_t& out_min, uint16_t& out_max) {
    const size_t n = window_full ? WINDOW_SIZE : window_idx;
    out_min = UINT16_MAX;
    out_max = 0;
    for (size_t i = 0; i < n; ++i) {
        if (window_raw[i] < out_min) out_min = window_raw[i];
        if (window_raw[i] > out_max) out_max = window_raw[i];
    }
}

// -------- Arduino entry points -----------------------------------------
void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) { /* wait up to 2 s for USB CDC */ }

    // 12-bit resolution (0..4095) is the ESP32 default, but set it
    // explicitly so the reader of this code does not have to guess.
    analogReadResolution(12);

    // 11 dB attenuation → full-scale input range ~0..3.3 V.
    // Without this, the sensor's ~2.5 V dry reading gets clipped.
    analogSetPinAttenuation(PIN_MOISTURE, ADC_11db);

    log_i("Moisture characterization sketch starting...");
    log_i("Sampling every %lu ms, window = %u samples",
          SAMPLE_PERIOD_MS, (unsigned)WINDOW_SIZE);

    // CSV header — easy to paste into a spreadsheet or gnuplot later.
    Serial.println();
    Serial.println("t_ms,raw,mV,avg_raw,min_raw,max_raw,spread_raw");
}

void loop() {
    const uint32_t t_start = millis();

    const uint16_t raw = analogRead(PIN_MOISTURE);
    const uint32_t mV  = analogReadMilliVolts(PIN_MOISTURE);
    const float    avg = movingAverage(raw);

    uint16_t w_min, w_max;
    computeMinMax(w_min, w_max);

    Serial.printf("%lu,%u,%lu,%.1f,%u,%u,%u\n",
                  t_start, raw, mV, avg, w_min, w_max,
                  (uint16_t)(w_max - w_min));

    // Simple periodic pacing. delay() is OK here because this sketch
    // has nothing else to do — in the final firmware we will replace
    // this with vTaskDelayUntil() inside a FreeRTOS task.
    const uint32_t elapsed = millis() - t_start;
    if (elapsed < SAMPLE_PERIOD_MS) {
        delay(SAMPLE_PERIOD_MS - elapsed);
    }
}