#include "WiFiManager.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip4_addr.h"
#include <cstdio>
#include <cstring>

// WiFi credentials from build-time configuration
#ifndef WIFI_SSID
#error "WIFI_SSID not defined - set WIFI_SSID environment variable"
#endif

#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD not defined - set WIFI_PASSWORD environment variable"
#endif

WiFiManager::WiFiManager() :
      status_(Status::UNINITIALIZED),
      initialized_(false),
      last_poll_time_(0),
      connect_start_time_(0)
{
}

WiFiManager::~WiFiManager()
{
    if (initialized_) {
        cyw43_arch_deinit();
    }
}

bool WiFiManager::init()
{
    if (initialized_) {
        return true;
    }

    status_ = Status::INITIALIZING;

    printf("%s: Initializing CYW43 (pico_cyw43_arch_lwip_threadsafe_background)...\n", __FUNCTION__);

    if (cyw43_arch_init()) {
        printf("%s: Failed to initialize CYW43\n", __FUNCTION__);
        status_ = Status::FAILED;
        return false;
    }

    cyw43_arch_enable_sta_mode();
    initialized_ = true;
    status_ = Status::INITIALIZED;

    printf("%s: Initialized successfully\n", __FUNCTION__);

    return true;
}

void WiFiManager::setInitialized()
{
    initialized_ = true;
    status_ = Status::INITIALIZED;
    cyw43_arch_enable_sta_mode();
}

bool WiFiManager::connect()
{
    if (!initialized_) {
        printf("%s: Not initialized - call init() first\n", __FUNCTION__);
        return false;
    }

    if (status_ == Status::CONNECTING || status_ == Status::CONNECTED) {
        return true;  // Already connecting or connected
    }

    status_ = Status::CONNECTING;
    connect_start_time_ = time_us_32() / 1000;  // Convert to ms

    printf("%s: Connecting to '%s'...\n", WIFI_SSID);

    // Use async connect (non-blocking, FreeRTOS-compatible)
    // Status will be checked in poll() method
    int result = cyw43_arch_wifi_connect_async(
        WIFI_SSID,
        WIFI_PASSWORD,
        CYW43_AUTH_WPA2_AES_PSK
    );

    if (result != 0) {
        printf("%s: Connect initiation failed with error %d\n", result);
        status_ = Status::FAILED;
        return false;
    }

    printf("%s: Connection initiated, waiting for link...\n", __FUNCTION__);
    return true;
}

void WiFiManager::disconnect()
{
    if (!initialized_ || status_ == Status::DISCONNECTED) {
        return;
    }

    printf("%s: Disconnecting...\n", __FUNCTION__);

    // Note: cyw43_arch doesn't have explicit disconnect in threadsafe_background mode
    // We'd need to deinit and reinit to truly disconnect. For now, just update status.
    status_ = Status::DISCONNECTED;
}

bool WiFiManager::isConnected() const
{
    if (!initialized_) {
        return false;
    }

    // Check link status
    int link_status = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    return (link_status == CYW43_LINK_UP);
}

bool WiFiManager::getIPAddress(char* out, size_t outlen) const
{
    if (!initialized_ || !isConnected() || outlen < 16) {
        return false;
    }

    const ip4_addr_t* addr = netif_ip4_addr(netif_default);
    if (addr == nullptr) {
        return false;
    }

    snprintf(out, outlen, "%s", ip4addr_ntoa(addr));
    return true;
}

bool WiFiManager::getMACAddress(char* out, size_t outlen) const
{
    if (!initialized_ || outlen < 18) {
        return false;
    }

    uint8_t mac[6];
    cyw43_hal_get_mac(CYW43_ITF_STA, mac);

    snprintf(out, outlen, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return true;
}

void WiFiManager::poll()
{
    if (!initialized_) {
        return;
    }

    uint32_t now = time_us_32() / 1000;  // Convert to ms

    // Poll every 1 second
    if ((now - last_poll_time_) < 1000) {
        return;
    }

    last_poll_time_ = now;

    // Check connection status
    bool connected = isConnected();

    // Update status based on link
    if (status_ == Status::CONNECTED && !connected) {
        printf("%s: Connection lost\n", __FUNCTION__);
        status_ = Status::DISCONNECTED;
    } else if (status_ == Status::CONNECTING) {
        if (connected) {
            status_ = Status::CONNECTED;

            char ip[16];
            if (getIPAddress(ip, sizeof(ip))) {
                printf("%s: Connected! IP: %s\n", ip);
            }
        } else {
            // Check timeout (30 seconds)
            if ((now - connect_start_time_) > 30000) {
                printf("%s: Connect timeout\n", __FUNCTION__);
                status_ = Status::FAILED;
            }
        }
    }
}
