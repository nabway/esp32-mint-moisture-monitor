#include <Arduino.h>
#include "config/pins.hpp"
#include "config/secrets.hpp"
#include "config/pins.hpp"
#include "config/calibration.hpp"
#include "sensor/moisture.hpp"
#include "net/wifi_manager.hpp"
#include "net/mqtt_client.hpp"
#include "cloud/telegram_bot.hpp"

// ── Config ────────────────────────────────────────────────────────────────────

static constexpr uint32_t SAMPLE_INTERVAL_MS  = 15000;   // read sensor every 15 s
static constexpr uint32_t PUBLISH_INTERVAL_MS = 30000;  // publish MQTT every 30 s
static constexpr uint8_t  ALERT_BELOW_PCT     = 50;     // telegram alert threshold

// ── State ─────────────────────────────────────────────────────────────────────
// Latched LED state, updated with hysteresis (see loop()).
static bool alert_active = false;
static sensor::MoistureSensor moisture(pins::MOISTURE_ADC);

static uint32_t last_sample_ms  = 0;
static uint32_t last_publish_ms = 0;

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) {}

    log_i("=== Menta Monitor v2.0.0 boot ===");

    pinMode(pins::ONBOARD_LED, OUTPUT);
    digitalWrite(pins::ONBOARD_LED, HIGH);  // start OFF
    
    moisture.begin();

    if (!net::WiFiManager::begin(30000)) {
        log_e("WiFi failed — rebooting in 5 s");
        delay(5000);
        ESP.restart();
    }

    if (!net::MqttClient::begin()) {
        // Non-fatal: MQTT will retry in loop()
        log_w("MQTT not connected on boot — will retry");
    }

    // Boot notification so you know the device came online
    cloud::TelegramBot::send("🌿 Menta Monitor online");

    log_i("Setup complete");
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
    const uint32_t now = millis();

    // ── Keep connections alive ────────────────────────────────────────────────
    net::WiFiManager::loop();
    net::MqttClient::loop();
    const auto r = moisture.sample();
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

    // ── Sample sensor ─────────────────────────────────────────────────────────
    if (now - last_sample_ms >= SAMPLE_INTERVAL_MS) {
        last_sample_ms = now;

        const char* state_str = sensor::toString(r.state);

        log_i("moisture: %u%% | raw: %u | avg: %.1f | state: %s",
              r.percent, r.raw, r.raw_avg, state_str);

        // ── Telegram alert check alerts and polling ───────────────────────────
        cloud::TelegramBot::checkStateChange(r);
        cloud::TelegramBot::poll(r);

        // ── MQTT publish (throttled) ──────────────────────────────────────────
        if (now - last_publish_ms >= PUBLISH_INTERVAL_MS) {
            last_publish_ms = now;

            if (net::MqttClient::publishMoisture(r.percent, r.raw, state_str)) {
                log_d("MQTT publish OK");
            }
        }
    }
}
