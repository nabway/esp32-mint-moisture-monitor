#include "net/mqtt_client.hpp"
#include "config/secrets.hpp"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

static constexpr uint32_t RECONNECT_INTERVAL_MS = 10000;  // 10 s between retries
static uint32_t last_reconnect_ms = 0;


namespace net {

// ── Internal state ────────────────────────────────────────────────────────────

static WiFiClientSecure tls_client;
static PubSubClient     mqtt(tls_client);

// ── Topics ────────────────────────────────────────────────────────────────────

static constexpr char TOPIC_MOISTURE[] = "menta/moisture";

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool reconnect() {
    if (mqtt.connected()) return true;

    const uint32_t now = millis();
    if (now - last_reconnect_ms < RECONNECT_INTERVAL_MS) return false;
    last_reconnect_ms = now;

    log_i("MQTT connecting to %s...", secrets::MQTT_HOST);

    if (!mqtt.connect(secrets::MQTT_CLIENT_ID,
                      secrets::MQTT_USER,
                      secrets::MQTT_PASSWORD)) {
        log_e("MQTT connect failed, rc=%d", mqtt.state());
        return false;
    }

    log_i("MQTT connected");
    return true;
}

// ── Public ────────────────────────────────────────────────────────────────────

bool MqttClient::begin() {
    // HiveMQ Cloud uses a valid CA — skip full cert validation for simplicity.
    // For production, load the ISRG Root X1 certificate.
    tls_client.setInsecure();

    mqtt.setServer(secrets::MQTT_HOST, secrets::MQTT_PORT);
    mqtt.setKeepAlive(60);
    mqtt.setBufferSize(512);

    return reconnect();
}

void MqttClient::loop() {
    if (WiFi.status() != WL_CONNECTED) return;

    if (!mqtt.connected()) {
        reconnect();
    }
    mqtt.loop();
}

bool MqttClient::isConnected() {
    return mqtt.connected();
}

bool MqttClient::publishMoisture(uint8_t percent, uint16_t raw, const char* state) {
    if (!mqtt.connected()) return false;

    // Build compact JSON payload
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"percent\":%u,\"raw\":%u,\"state\":\"%s\"}",
             percent, raw, state);

    const bool ok = mqtt.publish(TOPIC_MOISTURE, payload, /*retained=*/true);
    if (ok) {
        log_d("MQTT publish OK → %s : %s", TOPIC_MOISTURE, payload);
    } else {
        log_w("MQTT publish failed");
    }
    return ok;
}

}  // namespace net
