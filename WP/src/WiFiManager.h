#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "pico/cyw43_arch.h"
#include "lwip/ip_addr.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

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
        // WiFi Inactive states
        UNINITIALIZED,          // must be declared before CHECK_WIFI_ALLOWED
        REBOOT_CYW43,           // must be declared before CHECK_WIFI_ALLOWED

        CHECK_WIFI_ALLOWED,
        DISCONNECTING,          // DISCONNECTING must be declared as the last inactive state

        // WiFi Active states: must be declared after the inactive states
        WIFI_POWERING_UP,
        CONNECTING,
        WAITING_FOR_IP,
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
     *  Returns true only if we have a Link AND an IP
     */
    bool isReady() const { return state_ == State::CONNECTED; }

    /**
     * Returns the current state for more granular debugging
     */
    //State getState() const { return state_; }


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
     * Get lwIP network interface for WiFi station mode.
     * Used by NetworkManager to initialize mDNS and HTTP server.
     *
     * @return Pointer to netif structure, or nullptr if not initialized
     */
    struct netif* getNetif() const;

    /**
     * This task manages the basic wifi connection state machine.
     */
    void WiFiManager_task();

    /**
     * Set the server hostname/IP address for check-in notifications.
     * Call this before WiFi connects to enable automatic check-in.
     *
     * Supports both IP addresses and hostnames (including mDNS names):
     * - IP address: "192.168.1.100"
     * - Hostname: "myserver.local" or "umod4-server.local"
     *
     * @param server_hostname Server hostname or IP address
     * @param server_port Server UDP port (default: 8081)
     */
    void setServerAddress(const char* server_hostname, uint16_t server_port = 8081);

    /**
     * Manually trigger a check-in notification to the server.
     * Called by Logger when a new log file is created.
     * Safe to call even if not connected - will be ignored.
     */
    void triggerCheckIn();

private:
    void sendCheckInNotification();
    static void heartbeatTimerCallback(TimerHandle_t xTimer);

    State state_;
    bool connected_;
    bool initialized_;
    TaskHandle_t taskHandle_;

    // Server address for check-in notifications
    char serverHostname_[64];  // Hostname or IP address
    uint16_t serverPort_;
    bool hasServerAddress_;

    // Periodic heartbeat timer (5 minutes)
    TimerHandle_t heartbeatTimer_;
};

#endif // WIFI_MANAGER_H
