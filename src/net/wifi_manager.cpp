#include "net/wifi_manager.hpp"
#include "config/secrets.hpp"
#include <Arduino.h>
#include <WiFi.h>

static const struct { const char* ssid; const char* pass; }
NETWORKS[] = {
    { secrets::WIFI_SSID,   secrets::WIFI_PASSWORD   },
    { secrets::WIFI_SSID_2, secrets::WIFI_PASSWORD_2 },
    { secrets::WIFI_SSID_3, secrets::WIFI_PASSWORD_3 }
};
static constexpr size_t NETWORK_COUNT = sizeof(NETWORKS) / sizeof(NETWORKS[0]);

namespace net {

bool WiFiManager::begin(uint32_t timeout_ms) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    for (size_t i = 0; i < NETWORK_COUNT; i++) {
        log_i("Trying '%s'...", NETWORKS[i].ssid);
        WiFi.begin(NETWORKS[i].ssid, NETWORKS[i].pass);
        const uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - start > timeout_ms / NETWORK_COUNT) {
                WiFi.disconnect(true);
                vTaskDelay(pdMS_TO_TICKS(500));  // wait clean
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        if (WiFi.status() == WL_CONNECTED) {
            log_i("Connected to '%s' — IP: %s",
                NETWORKS[i].ssid, WiFi.localIP().toString().c_str());
            return true;
        }
    }
    log_e("All networks failed");
    return false;
}

void WiFiManager::loop() {
    if (WiFi.status() == WL_CONNECTED) return;

    const uint32_t now = millis();
    static uint32_t last_attempt_ms = 0;
    static constexpr uint32_t RECONNECT_INTERVAL_MS = 15000;

    if (now - last_attempt_ms < RECONNECT_INTERVAL_MS) return;
    last_attempt_ms = now;

    log_w("WiFi lost — scanning known networks...");
    WiFi.disconnect(true);
    vTaskDelay(pdMS_TO_TICKS(500));

    for (size_t i = 0; i < NETWORK_COUNT; i++) {
        log_i("Trying '%s'...", NETWORKS[i].ssid);
        WiFi.begin(NETWORKS[i].ssid, NETWORKS[i].pass);
        const uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - start > 8000) {
                WiFi.disconnect(true);
                vTaskDelay(pdMS_TO_TICKS(500));
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        if (WiFi.status() == WL_CONNECTED) {
            log_i("Reconnected to '%s' — IP: %s",
                NETWORKS[i].ssid, WiFi.localIP().toString().c_str());
            return;
        }
    }
    log_e("All networks failed — will retry in %u s", RECONNECT_INTERVAL_MS / 1000);
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

}  // namespace net
