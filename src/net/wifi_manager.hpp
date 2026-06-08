#pragma once
#include <cstdint>
/**
 * @file wifi_manager.hpp
 * @brief WiFi connection with automatic reconnect.
 */

namespace net {

class WiFiManager {
public:
    // Connect on first call. Blocks until connected or timeout (ms).
    static bool begin(uint32_t timeout_ms = 15000);

    // Call from loop() — reconnects silently if connection dropped.
    static void loop();

    static bool isConnected();
};

}  // namespace net
