#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "pico/cyw43_arch.h"
#include "lwip/ip_addr.h"

#include "FreeRTOS.h"
#include "task.h"

/**
 * WiFi connection manager for umod4 WP.
 * This class manages deciding when WiFi mode is allowed.
 * If allowed, it will manage the basic connection to an Access Point (AP) ONLY.
 *
 * Handles WiFi initialization, connection, and status monitoring.
 * Uses pico_cyw43_arch_lwip_threadsafe_background for non-blocking operation.
 */
class WiFiManager {
public:
    enum class State {
        // Inactive states: DISCONNECTING must be declared as the last inactive state
        UNINITIALIZED,
        CHECK_WIFI_ALLOWED,
        DISCONNECTING,

        // Active states: must be declared after the inactive states
        WIFI_POWERING_UP,
        CONNECTING,
        CONNECTED
        };

    WiFiManager();
    ~WiFiManager();

    /**
     * Initialize WiFi hardware and lwIP stack.
     * Must be called before connect().
     *
     * @return true on success, false on failure
     */
    bool init();

    bool connect();

    /**
     * Disconnect from WiFi network.
     */
    void disconnect();

    /**
     * Check if currently connected to WiFi.
     */
    bool isConnected() const;

    /**
     * Get current WiFi status.
     */
    bool connected() const { return connected_; }

    /**
     * Get IP address (only valid when connected).
     *
     * @param out Output buffer for IP address string (min 16 bytes)
     * @return true if connected and IP retrieved, false otherwise
     */
    bool getIPAddress(char* out, size_t outlen) const;

    /**
     * Get WiFi MAC address.
     *
     * @param out Output buffer for MAC address string (min 18 bytes, format: "XX:XX:XX:XX:XX:XX")
     * @return true on success, false on error
     */
    bool getMACAddress(char* out, size_t outlen) const;

    /**
     * This task manages the basic wifi connection state machine.
     */
    void WiFiManager_task();

private:
    State state_;
    bool connected_;
    bool initialized_;
    TaskHandle_t taskHandle_;
};

#endif // WIFI_MANAGER_H
