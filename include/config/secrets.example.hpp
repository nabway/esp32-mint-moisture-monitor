#pragma once

/**
 * @file secrets.example.hpp
 * @brief Template for secrets.hpp — commit this file, NOT secrets.hpp.
 *
 * Usage:
 *   cp include/config/secrets.example.hpp include/config/secrets.hpp
 *   # then edit secrets.hpp with your real credentials
 *
 * Every constant below is referenced by the firmware, so all of them must
 * exist even if you only use one WiFi network or one Telegram chat. To use
 * fewer, repeat a value you already have instead of deleting the entry.
 */

namespace secrets {

// ── WiFi ─────────────────────────────────────────────────────────────────────
// Tried in order at boot and on every reconnect (net/wifi_manager.cpp).
constexpr char WIFI_SSID[]     = "YOUR_WIFI_SSID";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

constexpr char WIFI_SSID_2[]     = "YOUR_SECOND_WIFI_SSID";
constexpr char WIFI_PASSWORD_2[] = "YOUR_SECOND_WIFI_PASSWORD";

constexpr char WIFI_SSID_3[]     = "YOUR_THIRD_WIFI_SSID";
constexpr char WIFI_PASSWORD_3[] = "YOUR_THIRD_WIFI_PASSWORD";

// ── HiveMQ Cloud (TLS, port 8883) ────────────────────────────────────────────
constexpr char MQTT_HOST[]     = "YOUR_HOST.s1.eu.hivemq.cloud";
constexpr int  MQTT_PORT       = 8883;
constexpr char MQTT_USER[]     = "YOUR_MQTT_USER";
constexpr char MQTT_PASSWORD[] = "YOUR_MQTT_PASSWORD";

// Must be unique per device on the broker.
constexpr char MQTT_CLIENT_ID[] = "esp32-menta-001";

// ── Telegram ─────────────────────────────────────────────────────────────────
// Both chats receive every alert (cloud/telegram_bot.cpp).
constexpr char TELEGRAM_TOKEN[]     = "YOUR_BOT_TOKEN";
constexpr char TELEGRAM_CHAT_ID[]   = "YOUR_CHAT_ID";
constexpr char TELEGRAM_CHAT_ID_2[] = "YOUR_SECOND_CHAT_ID";

}  // namespace secrets
