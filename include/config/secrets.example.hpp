#pragma once

/**
 * @file secrets.hpp
 * @brief WiFi, MQTT and Telegram credentials.
 *
 * NEVER commit this file. It is listed in .gitignore.
 * This is a template for secrets.hpp for the repo.
 */

namespace secrets {

// ── WiFi ─────────────────────────────────────────────────────────────────────
constexpr char WIFI_SSID[]     = "YOUR_WIFI_SSID";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

// ── HiveMQ Cloud (TLS, port 8883) ────────────────────────────────────────────
constexpr char MQTT_HOST[]     = "YOUR_HOST.s1.eu.hivemq.cloud";
constexpr int  MQTT_PORT       = 8883;
constexpr char MQTT_USER[]     = "YOUR_MQTT_USER";
constexpr char MQTT_PASSWORD[] = "YOUR_MQTT_PASSWORD";

// Client ID debe ser único por dispositivo
constexpr char MQTT_CLIENT_ID[] = "esp32-menta-001";

// ── Telegram ─────────────────────────────────────────────────────────────────
constexpr char TELEGRAM_TOKEN[]   = "YOUR_BOT_TOKEN";
constexpr char TELEGRAM_CHAT_ID[] = "YOUR_CHAT_ID";

}  // namespace secrets
