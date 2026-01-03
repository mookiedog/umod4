#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "pico/cyw43_arch.h"
#include "lwip/ip_addr.h"

/**
 * WiFi connection manager for umod4 WP.
 * This refers to the basic connection to an Access Point (AP) only.
 *
 * Handles WiFi initialization, connection, and status monitoring.
 * Uses pico_cyw43_arch_lwip_threadsafe_background for non-blocking operation.
 */
class WiFiManager {
public:
    enum class Status {
        UNINITIALIZED,
        INITIALIZING,
        INITIALIZED,
        CONNECTING,
        CONNECTED,
        DISCONNECTED,
        FAILED
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

    /**
     * Connect to WiFi network using compile-time SSID/password.
     * Non-blocking - use isConnected() or getStatus() to check progress.
     *
     * @return true if connection attempt started, false on error
     */
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
    Status getStatus() const { return status_; }

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
     * Poll WiFi status (call periodically from main loop).
     * Updates internal status.
     */
    void poll();

    /**
     * Mark WiFi as initialized (when cyw43_arch_init() called externally).
     * Used when driver initialized in main() before FreeRTOS scheduler starts.
     */
    void setInitialized();

private:
    Status status_;
    bool initialized_;
    uint32_t last_poll_time_;
    uint32_t connect_start_time_;
};

#endif // WIFI_MANAGER_H
