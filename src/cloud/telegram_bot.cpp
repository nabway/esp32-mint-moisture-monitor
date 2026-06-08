#include "cloud/telegram_bot.hpp"
#include "config/secrets.hpp"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace cloud {

// ── Internal state ────────────────────────────────────────────────────────────

static constexpr char TG_API[] = "https://api.telegram.org";

static sensor::MoistureState last_state  = sensor::MoistureState::Unknown;
static int32_t               last_update = 0;   // Telegram update_id
static uint32_t              last_poll_ms = 0;
static constexpr uint32_t    POLL_INTERVAL_MS = 3000;

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool postMessage(const char* text) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    char url[128];
    snprintf(url, sizeof(url), "%s/bot%s/sendMessage", TG_API, secrets::TELEGRAM_TOKEN);

    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    char body[512];
    snprintf(body, sizeof(body),
             "{\"chat_id\":\"%s\",\"text\":\"%s\"}",
             secrets::TELEGRAM_CHAT_ID, text);         
    const int code = http.POST(body);
    http.end();

    snprintf(body, sizeof(body),
             "{\"chat_id\":\"%s\",\"text\":\"%s\"}",
             secrets::TELEGRAM_CHAT_ID_2, text);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.POST(body);
    http.end();


    if (code == 200) { log_i("Telegram sent OK"); return true; }
    log_w("Telegram failed, HTTP %d", code);
    return false;
}

static const char* stateEmoji(sensor::MoistureState s) {
    switch (s) {
        case sensor::MoistureState::Overwatered: return "💧";
        case sensor::MoistureState::Ok:          return "✅";
        case sensor::MoistureState::WaterSoon:   return "🌤";
        case sensor::MoistureState::WaterNow:    return "⚠️";
        case sensor::MoistureState::Critical:    return "🚨";
        default:                                 return "❓";
    }
}

static void sendStatusMessage(const sensor::MoistureReading& r) {
    char msg[200];
    const uint32_t uptime_s  = millis() / 1000;
    const uint32_t uptime_m  = uptime_s / 60;
    const uint32_t uptime_h  = uptime_m / 60;

    snprintf(msg, sizeof(msg),
             "%s Menta status\n"
             "Moisture: %u%% — %s\n"
             "Raw ADC: %u\n"
             "Uptime: %luh %02lum",
             stateEmoji(r.state),
             r.percent,
             sensor::toString(r.state),
             r.raw,
             uptime_h, uptime_m % 60);

    postMessage(msg);
}

// ── Public ────────────────────────────────────────────────────────────────────

bool TelegramBot::send(const char* message) {
    return postMessage(message);
}

void TelegramBot::checkStateChange(const sensor::MoistureReading& r) {
    // First sample — set baseline, no alert
    if (last_state == sensor::MoistureState::Unknown) {
        last_state = r.state;
        return;
    }

    // No change — do nothing
    if (r.state == last_state) return;

    // State changed — build and send alert
    char msg[200];
    const uint32_t uptime_s = millis() / 1000;
    const uint32_t uptime_m = uptime_s / 60;
    const uint32_t uptime_h = uptime_m / 60;

    snprintf(msg, sizeof(msg),
             "%s Menta state changed\n"
             "%s → %s\n"
             "Moisture: %u%%\n"
             "Uptime: %luh %02lum",
             stateEmoji(r.state),
             sensor::toString(last_state),
             sensor::toString(r.state),
             r.percent,
             uptime_h, uptime_m % 60);

    if (postMessage(msg)) {
        last_state = r.state;
    }
}

void TelegramBot::poll(const sensor::MoistureReading& r) {
    const uint32_t now = millis();
    if (now - last_poll_ms < POLL_INTERVAL_MS) return;
    last_poll_ms = now;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    char url[192];
    snprintf(url, sizeof(url),
             "%s/bot%s/getUpdates?offset=%ld&limit=1&timeout=0",
             TG_API, secrets::TELEGRAM_TOKEN, last_update + 1);

    http.begin(client, url);
    const int code = http.GET();
    if (code != 200) { http.end(); return; }

    // Parse JSON response
    JsonDocument doc;
    deserializeJson(doc, http.getStream());
    http.end();

    const JsonArray results = doc["result"].as<JsonArray>();
    if (results.size() == 0) return;

    // Process latest message
    const JsonObject update = results[0];
    last_update = update["update_id"].as<int32_t>();

    const char* text = update["message"]["text"] | "";
    log_i("Telegram command: %s", text);

    if (strcmp(text, "/status") == 0) {
        sendStatusMessage(r);
    } else if (strcmp(text, "/help") == 0) {
        postMessage("🌿 Menta Monitor commands:\n/status — current plant state\n/help — this message");
    }
}

}  // namespace cloud