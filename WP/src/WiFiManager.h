#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "pico/cyw43_arch.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "dhcpserver.h"
#include "Gps.h"

// Shared WiFi scan results — written by WiFiManager, read by /api/wifi-scan handler
#define WIFI_SCAN_MAX_RESULTS 20
struct WifiScanEntry {
    char ssid[33];
    int16_t rssi;
    uint8_t channel;
    uint8_t auth_mode;
};
struct WifiScanResults {
    bool scanning;          // scan currently in progress
    bool scan_requested;    // API or user has requested a new scan
    int count;
    WifiScanEntry entries[WIFI_SCAN_MAX_RESULTS];
};
extern WifiScanResults g_wifi_scan_results;

/**
 * WiFi connection manager for umod4 WP.
 * Manages when WiFi is allowed and handles both STA (client) and AP (access point) modes.
 *
 * Normal flow: CHECK_WIFI_ALLOWED → WIFI_POWERING_UP → CONNECTING → CONNECTED
 * AP fallback: if no credentials → AP_STARTING → AP_ACTIVE
 *              if STA connect fails 3x → AP_STARTING → AP_ACTIVE
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
        CONNECTED,

        // AP mode states (active — CYW43 is initialized)
        AP_STARTING,            // enabling AP mode, starting DHCP server
        AP_ACTIVE,              // running as access point, awaiting reboot
        AP_SCANNING,            // scanning for home network while AP is up
        RADIO_OFF,              // bike moving >20 MPH — radio shut down until power cycle
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
     * Returns true when network services can start:
     * either STA is connected (CONNECTED) or AP mode is active (AP_ACTIVE).
     * NetworkManager uses this to know when to start mDNS and HTTP.
     */
    bool isReady() const {
        return state_ == State::CONNECTED || state_ == State::AP_ACTIVE;
    }

    /**
     * Returns true when running as an access point (no STA connection).
     * Used to distinguish AP mode from STA mode in status reporting.
     */
    bool isInApMode() const { return state_ == State::AP_ACTIVE; }

    /**
     * Returns the resolved AP SSID currently being broadcast.
     * Only meaningful when isInApMode() is true.
     */
    const char* getApSSID() const { return ap_ssid_resolved_; }

    /**
     * Get IP address (only valid when connected in STA mode).
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
     * Get lwIP network interface for the active WiFi mode.
     * Returns AP netif (CYW43_ITF_AP) in AP mode, STA netif in STA mode.
     * Used by NetworkManager to initialize mDNS on the correct interface.
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
     * Set WiFi station credentials (SSID and password).
     * Must be called before the WiFiManager task reaches CONNECTING state.
     */
    void setCredentials(const char* ssid, const char* password);

    /**
     * Set GPS object for speed-based radio shutdown.
     * Call after both Gps and WiFiManager are constructed.
     */
    void setGps(Gps* gps) { gps_ = gps; }

    /**
     * Manually trigger a check-in notification to the server.
     * Called by Logger when a new log file is created.
     * Safe to call even if not connected - will be ignored.
     */
    void triggerCheckIn();

private:
    void sendCheckInNotification();
    void performScan();
    static void heartbeatTimerCallback(TimerHandle_t xTimer);
    static int scanResultCallback(void* env, const cyw43_ev_scan_result_t* result);

    State state_;
    bool connected_;
    bool initialized_;
    TaskHandle_t taskHandle_;

    Gps* gps_;
    int scan_countdown_;        // seconds until next background AP scan
    bool home_ssid_found_;      // set by scanResultCallback

    // WiFi station credentials
    char wifiSsid_[64];
    char wifiPassword_[64];

    // Server address for check-in notifications
    char serverHostname_[64];  // Hostname or IP address
    uint16_t serverPort_;
    bool hasServerAddress_;

    // Periodic heartbeat timer (5 minutes)
    TimerHandle_t heartbeatTimer_;

    // AP mode state
    dhcp_server_t ap_dhcp_server_;
    char          ap_ssid_resolved_[32];  // actual SSID being broadcast (MAC-derived or config)
    bool          ap_mode_active_;        // true when AP is up and DHCP server is running
};

#endif // WIFI_MANAGER_H
