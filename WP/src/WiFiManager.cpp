#include "WiFiManager.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip4_addr.h"
#include "lwip/udp.h"
#include "lwip/api.h"
#include <cstdio>
#include <cstring>

#include "umod4_WP.h"
#include "ota_flash_task.h"  // For ota_flash_in_progress()

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
    hasServerAddress_ = false;
    serverPort_ = 8081;
    serverHostname_[0] = '\0';
    heartbeatTimer_ = NULL;

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

    // Create periodic heartbeat timer (5 minutes, repeating)
    heartbeatTimer_ = xTimerCreate(
        "HeartbeatTimer",
        pdMS_TO_TICKS(5 * 60 * 1000),  // 5 minutes
        pdTRUE,  // Auto-reload (repeat)
        this,    // Pass 'this' pointer as timer ID
        heartbeatTimerCallback
    );

    if (heartbeatTimer_ == NULL) {
        printf("WiFiMgr: Warning - Failed to create heartbeat timer\n");
    }
}

WiFiManager::~WiFiManager()
{
    // Stop heartbeat timer if running
    if (heartbeatTimer_ != NULL) {
        xTimerStop(heartbeatTimer_, 0);
        xTimerDelete(heartbeatTimer_, 0);
    }

    cyw43_arch_deinit();
}

bool WiFiManager::getIPAddress(char* out, size_t outlen) const
{
    if (state_ != State::CONNECTED || outlen < 16) return false;

    // Use the CYW43 driver's netif for the station interface
    const ip4_addr_t* addr = netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]);

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

        // OTA Safety: If OTA flash is in progress, park this task completely.
        // The OTA task will shut down WiFi and we must NOT try to reinitialize it.
        if (ota_flash_in_progress()) {
            while (1);
        }

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

                        // Send initial check-in notification to server
                        sendCheckInNotification();

                        // Start periodic heartbeat timer (5 minutes)
                        if (heartbeatTimer_ != NULL) {
                            xTimerStart(heartbeatTimer_, 0);
                            printf("WiFiMgr: Started heartbeat timer (5 min interval)\n");
                        }
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
                if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP) {
                    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
                }
                else {
                    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
                    printf("WiFiMgr: Connection lost\n");
                    // Stop heartbeat timer when connection lost
                    if (heartbeatTimer_ != NULL) {
                        xTimerStop(heartbeatTimer_, 0);
                    }
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

void WiFiManager::setServerAddress(const char* server_hostname, uint16_t server_port)
{
    if (server_hostname && strlen(server_hostname) < sizeof(serverHostname_)) {
        strncpy(serverHostname_, server_hostname, sizeof(serverHostname_) - 1);
        serverHostname_[sizeof(serverHostname_) - 1] = '\0';
        serverPort_ = server_port;
        hasServerAddress_ = true;
        printf("WiFiMgr: Server address set to %s:%u\n", serverHostname_, serverPort_);
    }
}

void WiFiManager::sendCheckInNotification()
{
    if (!hasServerAddress_) {
        return;  // No server configured, skip check-in
    }

    // Get our MAC address
    char mac_str[18];
    uint8_t mac[6];
    cyw43_hal_get_mac(CYW43_HAL_MAC_WLAN0, mac);
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Get our IP address
    char ip_str[16];
    if (!getIPAddress(ip_str, sizeof(ip_str))) {
        printf("WiFiMgr: Cannot send check-in - no IP address\n");
        return;
    }

    // Build JSON payload: {"device_mac":"xx:xx:xx:xx:xx:xx","ip":"192.168.1.150"}
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"device_mac\":\"%s\",\"ip\":\"%s\"}",
             mac_str, ip_str);

    printf("WiFiMgr: Resolving server hostname: %s\n", serverHostname_);

    // Create UDP PCB (Protocol Control Block)
    struct udp_pcb* pcb = udp_new();
    if (!pcb) {
        printf("WiFiMgr: Failed to create UDP PCB\n");
        return;
    }

    // Resolve server hostname to IP address (supports both IPs and DNS/mDNS names)
    ip_addr_t server_addr;

    // First try to parse as literal IP address
    if (!ip4addr_aton(serverHostname_, &server_addr)) {
        // Not a literal IP, resolve via DNS/mDNS
        printf("WiFiMgr: Not a literal IP, resolving via DNS...\n");

        err_t err = netconn_gethostbyname(serverHostname_, &server_addr);
        if (err != ERR_OK) {
            printf("WiFiMgr: Failed to resolve %s (err=%d)\n", serverHostname_, err);
            udp_remove(pcb);
            return;
        }

        printf("WiFiMgr: Resolved %s to %s\n", serverHostname_, ip4addr_ntoa(&server_addr));
    } else {
        printf("WiFiMgr: Using literal IP address: %s\n", ip4addr_ntoa(&server_addr));
    }

    printf("WiFiMgr: Sending check-in to %s:%u\n", ip4addr_ntoa(&server_addr), serverPort_);
    printf("WiFiMgr: Payload: %s\n", payload);

    // Allocate pbuf for payload
    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, strlen(payload), PBUF_RAM);
    if (!p) {
        printf("WiFiMgr: Failed to allocate pbuf\n");
        udp_remove(pcb);
        return;
    }

    // Copy payload into pbuf
    memcpy(p->payload, payload, strlen(payload));

    // Send UDP packet
    err_t err = udp_sendto(pcb, p, &server_addr, serverPort_);
    if (err != ERR_OK) {
        printf("WiFiMgr: UDP send failed: %d\n", err);
    } else {
        printf("WiFiMgr: Check-in notification sent successfully\n");
    }

    // Clean up
    pbuf_free(p);
    udp_remove(pcb);
}

void WiFiManager::triggerCheckIn()
{
    // Safe to call anytime - sendCheckInNotification() checks if ready
    sendCheckInNotification();
}

void WiFiManager::heartbeatTimerCallback(TimerHandle_t xTimer)
{
    // Get WiFiManager instance from timer ID
    WiFiManager* mgr = static_cast<WiFiManager*>(pvTimerGetTimerID(xTimer));

    if (mgr) {
        mgr->sendCheckInNotification();
    }
}
