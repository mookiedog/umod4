#include "WiFiManager.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip4_addr.h"
#include <cstdio>
#include <cstring>

#include "umod4_WP.h"

// WiFi credentials from build-time configuration
#ifndef WIFI_SSID
#error "WIFI_SSID not defined - set WIFI_SSID environment variable"
#endif

#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD not defined - set WIFI_PASSWORD environment variable"
#endif


// ----------------------------------------------------------------------------------
extern "C" void start_wifiMgr_task(void *pvParameters);

void start_wifiMgr_task(void *pvParameters)
{
    // The task parameter is the specific object instance we should be using in the ISR
    WiFiManager* m = static_cast<WiFiManager*>(pvParameters);

    // This allows us to invoke the task method on the correct instance
    m->WiFiManager_task();
    panic(LOCATION("Task should never return"));
}



WiFiManager::WiFiManager()
{
    BaseType_t err;

    connected_ = false;
    taskHandle_ = NULL;

    if (taskHandle_ == NULL) {
        err = xTaskCreate(
            start_wifiMgr_task,
            "WiFiMgrTask",
            2048,
            this,
            TASK_NORMAL_PRIORITY,
            &taskHandle_
        );
        if (err != pdPASS) {
            printf("%s: WARNING - Failed to create WiFiManager task\n", __FUNCTION__);
            taskHandle_ = NULL;
            panic("Unable to create WiFiManager task");
        }
        // Set core affinity to core 0 - async context will pin its task to same core
        // This prevents the assert failure: get_core_num() == async_context_core_num()
        vTaskCoreAffinitySet(taskHandle_, (1 << 0));
    }
}

WiFiManager::~WiFiManager()
{
    if (initialized_) {
        cyw43_arch_deinit();
    }
}

#if 0
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
#endif

// ----------------------------------------------------------------------------------
void WiFiManager::WiFiManager_task()
{
    state_ = State::UNINITIALIZED;

    while (true) {

        // If we are in any active state, then we need to verify that WiFi is still allowed.
        // If not, we need to disconnect immediately.
        if (static_cast<int>(state_) > static_cast<int>(State::DISCONNECTING)) {
            // WL_GPIO is guaranteed to be active an any active state
            bool wifiAllowed = (cyw43_arch_gpio_get (CYW43_WL_GPIO_VBUS_PIN) != 0);
            if (!wifiAllowed) {
                printf("%s: WiFi no longer allowed, disconnecting\n", __FUNCTION__);
                state_ = State::DISCONNECTING;
            }
        }

        switch (state_) {
            case State::UNINITIALIZED:
                // This call initializes the CYW43 driver but leaves the WiFi interface powered down.
                printf("%s: Initializing CYW43...\n", __FUNCTION__);
                if (cyw43_arch_init()) {
                    printf("%s: Failed to initialize CYW43\n", __FUNCTION__);
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
                else {
                    state_ = State::CHECK_WIFI_ALLOWED;
                }
                break;

            case State::CHECK_WIFI_ALLOWED:
                // Once in this state, the GPIO controlled by the CYW43 is available for use.
                // If WiFi is allowed, proceed to connect, otherwise wait and recheck
                if ((cyw43_arch_gpio_get (CYW43_WL_GPIO_VBUS_PIN) != 0)) {
                    // We can only power up if we see VBUS high, indicating USB power is present
                    printf("%s: WiFi allowed, powering up\n", __FUNCTION__);
                    state_ = State::WIFI_POWERING_UP;
                } else {
                    printf("%s: WiFi not allowed, retrying...\n", __FUNCTION__);
                    vTaskDelay(pdMS_TO_TICKS(5000));
                }
                break;

            case State::DISCONNECTING:
                printf("%s: Disconnecting & powering down WiFi unit\n", __FUNCTION__);
                cyw43_arch_disable_sta_mode();
                state_ = State::CHECK_WIFI_ALLOWED;
                break;

            case State::WIFI_POWERING_UP:
                // Enable station mode: we will connect to an access point as a client
                // This call has the side effect of powering up the WiFi interface.
                cyw43_arch_enable_sta_mode();
                printf("%s: Powering up WiFi into station mode\n", __FUNCTION__);
                state_ = State::CONNECTING;
                break;

            case State::CONNECTING:
                // Attempt to connect to the WiFi access point
                printf("%s: Connecting to '%s'... (%s)\n", __FUNCTION__, WIFI_SSID, "<passwd>"/*WIFI_PASSWORD*/);

                // Use blocking connect - works in FreeRTOS task with sys_freertos variant
                {
                    int err = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000);
                    if (err != 0) {
                        printf("%s: Connection failed: %d\n", __FUNCTION__, err);
                    }
                    else {
                        printf("%s: Connected to AP\n", __FUNCTION__);
                        state_ = State::CONNECTED;
                    }
                }
                break;

            case State::CONNECTED:
                vTaskDelay(pdMS_TO_TICKS(250));
                break;

            default:
                // Should not reach here
                printf("%s: Unknown state %d: Disconnecting\n", __FUNCTION__, static_cast<int>(state_));
                state_ = State::DISCONNECTING;
                break;
        }
    }
}
