#include <Arduino.h>
#include "config/pins.hpp"
#include "config/secrets.hpp"
#include "config/calibration.hpp"
#include "sensor/moisture.hpp"
#include "sensor/light.hpp"
#include "net/wifi_manager.hpp"
#include "net/mqtt_client.hpp"
#include "cloud/telegram_bot.hpp"

// ── Config ────────────────────────────────────────────────────────────────────

static constexpr uint32_t SENSOR_INTERVAL_MS  = 1000;   // read the ADC every 1 s
static constexpr uint32_t REPORT_INTERVAL_MS  = 15000;  // log + Telegram every 15 s
static constexpr uint32_t PUBLISH_INTERVAL_MS = 30000;  // publish MQTT every 30 s

// Samples taken back-to-back in setup() to fill the moving-average window
// before the first reading is reported.
static constexpr size_t   BOOT_PRIME_SAMPLES  = 10;
static constexpr uint32_t BOOT_PRIME_DELAY_MS = 50;

// ── State ─────────────────────────────────────────────────────────────────────
// Latched LED state, updated with hysteresis (see updateAlertLed()).
static bool alert_active = false;
static sensor::MoistureSensor moisture(pins::MOISTURE_ADC);
static sensor::LightSensor    light;

static uint32_t last_sample_ms  = 0;
static uint32_t last_report_ms  = 0;
static uint32_t last_publish_ms = 0;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Hysteresis around the alert threshold:
//   ON  at <= PCT_ALERT_BELOW
//   OFF at >= PCT_ALERT_BELOW + PCT_HYSTERESIS
// Between the two the previous state is held.
static void updateAlertLed(const sensor::MoistureReading& r) {
    if (r.percent <= calibration::PCT_ALERT_BELOW) {
        alert_active = true;
    } else if (r.percent >=
               calibration::PCT_ALERT_BELOW + calibration::PCT_HYSTERESIS) {
        alert_active = false;
    }

    digitalWrite(pins::ONBOARD_LED, alert_active ? HIGH : LOW);
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) {}

    log_i("=== Menta Monitor v2.0.0 boot ===");

    pinMode(pins::ONBOARD_LED, OUTPUT);
    digitalWrite(pins::ONBOARD_LED, LOW);  // active HIGH, so LOW is OFF

    moisture.begin();

    // Non-fatal: if the BH1750 is missing, everything else keeps working and
    // light simply reports as unavailable.
    light.begin();

    if (!net::WiFiManager::begin(30000)) {
        log_e("WiFi failed — rebooting in 5 s");
        delay(5000);
        ESP.restart();
    }

    if (!net::MqttClient::begin()) {
        // Non-fatal: MQTT will retry in loop()
        log_w("MQTT not connected on boot — will retry");
    }

    // Fill the moving-average window so the boot report reflects the substrate
    // and not a single noisy conversion.
    for (size_t i = 0; i < BOOT_PRIME_SAMPLES; ++i) {
        moisture.sample();
        delay(BOOT_PRIME_DELAY_MS);
    }

    const sensor::MoistureReading boot_reading = moisture.last();
    const sensor::LightReading    boot_light   = light.sample();
    updateAlertLed(boot_reading);

    log_i("boot reading: %u%% | raw: %u | avg: %.1f | state: %s | lux: %.1f (valid: %d)",
          boot_reading.percent, boot_reading.raw, boot_reading.raw_avg,
          sensor::toString(boot_reading.state),
          boot_light.lux, boot_light.valid);

    // Boot notification. Reports the state the device woke up in, so a pot that
    // is already dry raises an alert instead of silently becoming the baseline.
    cloud::TelegramBot::bootReport(boot_reading, boot_light);

    const uint32_t now = millis();
    last_sample_ms  = now;
    last_report_ms  = now;
    last_publish_ms = now;

    log_i("Setup complete");
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
    const uint32_t now = millis();

    // ── Keep connections alive ────────────────────────────────────────────────
    net::WiFiManager::loop();
    net::MqttClient::loop();

    // ── Sample sensor at a fixed cadence ──────────────────────────────────────
    // One sample per second, so the 10-slot window spans ~10 s of real time.
    // Sampling on every loop iteration filled the window in microseconds, which
    // averaged ADC noise but gave no smoothing over time.
    if (now - last_sample_ms >= SENSOR_INTERVAL_MS) {
        last_sample_ms = now;
        updateAlertLed(moisture.sample());
        light.sample();
    }

    // ── Report ────────────────────────────────────────────────────────────────
    if (now - last_report_ms >= REPORT_INTERVAL_MS) {
        last_report_ms = now;

        const sensor::MoistureReading r = moisture.last();
        const sensor::LightReading    l = light.last();
        const char* state_str = sensor::toString(r.state);

        log_i("moisture: %u%% | raw: %u | avg: %.1f | state: %s | lux: %.1f (valid: %d)",
              r.percent, r.raw, r.raw_avg, state_str, l.lux, l.valid);

        // ── Telegram alert check and command polling ──────────────────────────
        cloud::TelegramBot::checkStateChange(r);
        cloud::TelegramBot::poll(r, l);

        // ── MQTT publish (throttled) ──────────────────────────────────────────
        if (now - last_publish_ms >= PUBLISH_INTERVAL_MS) {
            last_publish_ms = now;

            if (net::MqttClient::publishMoisture(r.percent, r.raw, state_str, l)) {
                log_d("MQTT publish OK");
            }
        }
    }
}
