#pragma once
#include <cstdint>
#include "sensor/moisture.hpp"

/**
 * @file telegram_bot.hpp
 * @brief Telegram alerts on state change + /status command polling.
 */

namespace cloud {

class TelegramBot {
public:
    // Send alert only when MoistureState changes. Call after every sample.
    static void checkStateChange(const sensor::MoistureReading& r);

    // Poll Telegram for incoming commands (e.g. /status). Call every ~3 s.
    static void poll(const sensor::MoistureReading& r);

    // Send any message unconditionally.
    static bool send(const char* message);
};

}  // namespace cloud
