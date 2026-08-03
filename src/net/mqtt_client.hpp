#pragma once
#include <cstdint>
#include "sensor/light.hpp"

/**
 * @file mqtt_client.hpp
 * @brief MQTT over TLS to HiveMQ Cloud.
 *
 * Publishes sensor readings to:
 *   menta/moisture   → JSON payload
 *
 * Call begin() once after WiFi is up.
 * Call loop() every iteration to maintain connection.
 */

namespace net {

class MqttClient {
public:
    static bool begin();

    // Keeps connection alive — call from loop().
    static void loop();

    static bool isConnected();

    // Publish one telemetry sample. Light is published as JSON null when the
    // BH1750 is absent or its last read failed. Returns false if not connected.
    static bool publishMoisture(uint8_t percent, uint16_t raw, const char* state,
                                const sensor::LightReading& light);
};

}  // namespace net
