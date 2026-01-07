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
    WiFiManager* m = static_cast<WiFiManager*>(pvParameters);
    m->WiFiManager_task();
    panic("WiFiManager Task should never return");
}

WiFiManager::WiFiManager()
{
    connected_ = false;
    taskHandle_ = NULL;
    state_ = State::UNINITIALIZED;

    BaseType_t err = xTaskCreate(
        start_wifiMgr_task,
        "WiFiMgrTask",
        2048,
        this,
        TASK_NORMAL_PRIORITY,
        &taskHandle_
    );

    if (err != pdPASS) {
        printf("WiFiMgr: Critical - Task creation failed\n");
        panic("Unable to create WiFiManager task");
    }

    // Pin to Core 0 for Pico SDK async safety
    vTaskCoreAffinitySet(taskHandle_, (1 << 0));
}

WiFiManager::~WiFiManager()
{
    cyw43_arch_deinit();
}

bool WiFiManager::getIPAddress(char* out, size_t outlen) const
{
    if (state_ != State::CONNECTED || outlen < 16) return false;

    // Use the SDK internal netif for the station interface
    extern struct netif nm_netif_sta;
    const ip4_addr_t* addr = netif_ip4_addr(&nm_netif_sta);

    if (addr && addr->addr != 0) {
        snprintf(out, outlen, "%s", ip4addr_ntoa(addr));
        return true;
    }
    return false;
}

struct netif* WiFiManager::getNetif() const
{
    if (state_ < State::WIFI_POWERING_UP) {
        return nullptr;
    }
    return &cyw43_state.netif[CYW43_ITF_STA];
}

// ----------------------------------------------------------------------------------
void WiFiManager::WiFiManager_task()
{
    uint32_t dhcp_start_time = 0;
    int fail_count = 0;

    while (true) {

        // Global Safety: Check if WiFi is allowed via VBUS GPIO
        // Only check if we are beyond the initial setup state
        if (state_ > State::CHECK_WIFI_ALLOWED) {
            bool wifiAllowed = (cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN) != 0);
            if (!wifiAllowed) {
                printf("WiFiMgr: VBUS power lost, disconnecting\n");
                state_ = State::DISCONNECTING;
            }
        }

        switch (state_) {
            case State::UNINITIALIZED:
                printf("WiFiMgr: Initializing hardware...\n");
                if (cyw43_arch_init()) {
                    printf("WiFiMgr: cyw43_arch_init failed!\n");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                else {
                    state_ = State::CHECK_WIFI_ALLOWED;
                }
                break;

            case State::CHECK_WIFI_ALLOWED:
                if (cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN) != 0) {
                    printf("WiFiMgr: Power OK, enabling Station Mode\n");
                    state_ = State::WIFI_POWERING_UP;
                } else {
                    // Poll slowly while waiting for USB power
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }
                break;

            case State::WIFI_POWERING_UP:
                cyw43_arch_enable_sta_mode();
                state_ = State::CONNECTING;
                break;

            case State::CONNECTING:
                printf("WiFiMgr: Connecting to SSID: %s\n", WIFI_SSID);
                // Blocking call with 30s timeout
                {
                    int err = cyw43_arch_wifi_connect_timeout_ms(
                                WIFI_SSID, WIFI_PASSWORD,
                                CYW43_AUTH_WPA2_AES_PSK, 30000);

                    if (err == 0) {
                        printf("WiFiMgr: Link Up, waiting for IP...\n");
                        state_ = State::WAITING_FOR_IP;
                        dhcp_start_time = xTaskGetTickCount();
                        fail_count = 0; // Reset error counter
                    } else {
                        printf("WiFiMgr: Connection failed (%d)\n", err);
                        fail_count++;

                        if (fail_count >= 3) {
                            state_ = State::REBOOT_CYW43;
                        } else {
                            vTaskDelay(pdMS_TO_TICKS(5000)); // Backoff
                        }
                    }
                }
                break;

            case State::WAITING_FOR_IP:
                {
                    struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
                    const ip4_addr_t* addr = netif_ip4_addr(n);

                    if (addr && addr->addr != 0) {
                        printf("WiFiMgr: Connected! IP: %s\n", ip4addr_ntoa(addr));

                        // Disable WiFi power save for minimum latency
                        // (Commented out - not needed for current use case)
                        //printf("WiFiMgr: Disabling WiFi power save\n");
                        //cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE << 4);

                        state_ = State::CONNECTED;
                    } else {
                        // Check for DHCP timeout (15 seconds)
                        if ((xTaskGetTickCount() - dhcp_start_time) > pdMS_TO_TICKS(15000)) {
                            printf("WiFiMgr: DHCP Timeout\n");
                            state_ = State::REBOOT_CYW43;
                        }
                        vTaskDelay(pdMS_TO_TICKS(250)); // Poll IP frequently
                    }
                }
                break;

            case State::CONNECTED:
                // Periodically check link integrity
                if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
                    printf("WiFiMgr: Connection lost\n");
                    state_ = State::CONNECTING;
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;

            case State::DISCONNECTING:
                cyw43_arch_disable_sta_mode();
                state_ = State::CHECK_WIFI_ALLOWED;
                break;

            case State::REBOOT_CYW43:
                printf("WiFiMgr: Hard resetting CYW43 chip...\n");
                cyw43_arch_deinit();
                vTaskDelay(pdMS_TO_TICKS(500));
                state_ = State::UNINITIALIZED;
                fail_count = 0;
                break;

            default:
                state_ = State::REBOOT_CYW43;
                break;
        }
    }
}
