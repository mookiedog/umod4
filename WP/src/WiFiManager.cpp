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
#include "FlashConfig.h"
#include "dhcpserver.h"
#include "dns_server.h"

// Set true while AP mode is active; checked by fs_custom.cpp to redirect unknown
// URIs to wifi_config.html, triggering the captive portal on phones/laptops.
bool g_captive_portal_active = false;

// Global WiFi scan results — written by WiFiManager, read by /api/wifi-scan handler
WifiScanResults g_wifi_scan_results = {};

// Device name and config globals owned by main.cpp
extern flash_config_t g_flash_config;
extern char g_device_name[64];

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
    wifiSsid_[0] = '\0';
    wifiPassword_[0] = '\0';
    heartbeatTimer_ = NULL;
    ap_ssid_resolved_[0] = '\0';
    ap_mode_active_ = false;
    memset(&ap_dhcp_server_, 0, sizeof(ap_dhcp_server_));
    gps_ = nullptr;
    scan_countdown_ = 60;
    home_ssid_found_ = false;

    static StackType_t  s_stack[1024];
    static StaticTask_t s_tcb;
    taskHandle_ = xTaskCreateStatic(
        start_wifiMgr_task,
        "WiFiMgrTask",
        1024,
        this,
        TASK_NORMAL_PRIORITY,
        s_stack, &s_tcb
    );

    if (taskHandle_ == NULL) {
        printf("WiFiMgr: Critical - Task creation failed\n");
        panic("Unable to create WiFiManager task");
    }

    // Create periodic heartbeat timer (5 minutes, repeating)
    static StaticTimer_t s_heartbeat_timer;
    heartbeatTimer_ = xTimerCreateStatic(
        "HeartbeatTimer",
        pdMS_TO_TICKS(5 * 60 * 1000),  // 5 minutes
        pdTRUE,  // Auto-reload (repeat)
        this,    // Pass 'this' pointer as timer ID
        heartbeatTimerCallback,
        &s_heartbeat_timer
    );
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

// ----------------------------------------------------------------------------------
// Scan result callback — called from cyw43 async context for each network found.
// Populates g_wifi_scan_results and sets home_ssid_found_ if home SSID matches.
int WiFiManager::scanResultCallback(void* env, const cyw43_ev_scan_result_t* result)
{
    if (!result) return 0;
    WiFiManager* self = static_cast<WiFiManager*>(env);

    if (strncmp((const char*)result->ssid, self->wifiSsid_, result->ssid_len) == 0
            && result->ssid_len == strlen(self->wifiSsid_)) {
        self->home_ssid_found_ = true;
    }

    // Build null-terminated SSID for comparison
    char ssid[33];
    memcpy(ssid, result->ssid, result->ssid_len);
    ssid[result->ssid_len] = '\0';

    // Skip hidden networks (empty SSID)
    if (ssid[0] == '\0') return 0;

    // Deduplicate by SSID — keep strongest signal per network name
    WifiScanResults& r = g_wifi_scan_results;
    for (int i = 0; i < r.count; i++) {
        if (strcmp(r.entries[i].ssid, ssid) == 0) {
            if (result->rssi > r.entries[i].rssi) {
                r.entries[i].rssi = result->rssi;
                r.entries[i].channel = result->channel;
            }
            return 0;
        }
    }
    if (r.count < WIFI_SCAN_MAX_RESULTS) {
        WifiScanEntry& e = r.entries[r.count++];
        memcpy(e.ssid, ssid, result->ssid_len + 1);
        e.rssi = result->rssi;
        e.channel = result->channel;
        e.auth_mode = result->auth_mode;
    }
    return 0;
}

// ----------------------------------------------------------------------------------
// Initiates a WiFi scan and blocks until complete (up to 10s).
// Updates g_wifi_scan_results and home_ssid_found_.
void WiFiManager::performScan()
{
    printf("WiFiMgr: Scanning for networks (home='%s')\n", wifiSsid_);
    home_ssid_found_ = false;
    g_wifi_scan_results.count = 0;
    g_wifi_scan_results.scanning = true;
    g_wifi_scan_results.scan_requested = false;

    cyw43_wifi_scan_options_t opts = {0};
    int err = cyw43_wifi_scan(&cyw43_state, &opts, this, scanResultCallback);
    if (err == 0) {
        uint32_t timeout = 10000 / 100;
        while (cyw43_wifi_scan_active(&cyw43_state) && timeout-- > 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    } else {
        printf("WiFiMgr: Scan failed (%d)\n", err);
    }
    g_wifi_scan_results.scanning = false;
    printf("WiFiMgr: Scan complete, %d networks found\n", g_wifi_scan_results.count);
}

// ----------------------------------------------------------------------------------
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
    if (state_ == State::AP_STARTING || state_ == State::AP_ACTIVE) {
        return &cyw43_state.netif[CYW43_ITF_AP];
    }
    if (state_ >= State::WIFI_POWERING_UP) {
        return &cyw43_state.netif[CYW43_ITF_STA];
    }
    return nullptr;
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


        switch (state_) {
            case State::UNINITIALIZED:
                printf("WiFiMgr: Initializing hardware...\n");
                if (cyw43_arch_init()) {
                    printf("WiFiMgr: cyw43_arch_init failed!\n");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                else {
                    // Enable STA mode to populate cyw43_state.mac — this is the earliest
                    // point the MAC becomes available (cyw43_arch_init alone does not read it).
                    // WIFI_POWERING_UP keeps its own enable_sta_mode() call for the
                    // reconnect-after-disconnect path; a double call is a no-op.
                    cyw43_arch_enable_sta_mode();

                    uint8_t mac[6];
                    cyw43_hal_get_mac(CYW43_HAL_MAC_WLAN0, mac);
                    printf("WiFiMgr: MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

                    bool name_needs_mac = (g_flash_config.device_name[0] == '\0' ||
                                           strcmp(g_flash_config.device_name, "umod4_0000") == 0);
                    if (name_needs_mac) {
                        snprintf(g_flash_config.device_name, sizeof(g_flash_config.device_name),
                                 "umod4_%02X%02X", mac[4], mac[5]);
                        snprintf(g_flash_config.ap_ssid, sizeof(g_flash_config.ap_ssid),
                                 "umod4_%02X%02X", mac[4], mac[5]);
                        strncpy(g_flash_config.ap_password, g_flash_config.device_name,
                                sizeof(g_flash_config.ap_password) - 1);
                        strncpy(g_device_name, g_flash_config.device_name,
                                sizeof(g_device_name) - 1);
                        flash_config_save(&g_flash_config);
                        printf("WiFiMgr: Device name set to '%s'\n", g_flash_config.device_name);
                    }
                    state_ = State::CHECK_WIFI_ALLOWED;
                }
                break;

            case State::CHECK_WIFI_ALLOWED:
                if (wifiSsid_[0] == '\0') {
                    printf("WiFiMgr: No WiFi credentials — entering AP mode\n");
                    state_ = State::AP_STARTING;
                } else {
                    printf("WiFiMgr: Enabling Station Mode\n");
                    state_ = State::WIFI_POWERING_UP;
                }
                break;

            case State::WIFI_POWERING_UP:
                cyw43_arch_enable_sta_mode();
                state_ = State::CONNECTING;
                break;

            case State::CONNECTING:
                printf("WiFiMgr: Connecting to SSID: %s\n", wifiSsid_);
                // Blocking call with 30s timeout
                {
                    int err = cyw43_arch_wifi_connect_timeout_ms(
                                wifiSsid_, wifiPassword_,
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
                            printf("WiFiMgr: STA connect failed %d times — falling back to AP mode\n", fail_count);
                            state_ = State::AP_STARTING;
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
                        printf("WiFiMgr: Disabling WiFi power save\n");
                        // CYW43_NONE_PM: Pure performance, no power savings
                        // CYW43_PERFORMANCE_PM: performance oriented, with some power savings
                        // CYW43_AGGRESSIVE_PM: aggressive power management, at cost of performance
                        cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
                        state_ = State::CONNECTED;

                        // NOTE: check-in is sent by NetworkManager after httpd_init()
                        // to avoid a race where the server tries HTTP before port 80 is listening.

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
                // Service on-demand scan requests while connected
                if (g_wifi_scan_results.scan_requested) {
                    performScan();
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;

            case State::DISCONNECTING:
                if (ap_mode_active_) {
                    g_captive_portal_active = false;
                    dns_server_deinit();
                    dhcp_server_deinit(&ap_dhcp_server_);
                    cyw43_arch_disable_ap_mode();
                    ap_mode_active_ = false;
                    printf("WiFiMgr: AP mode stopped\n");
                } else {
                    cyw43_arch_disable_sta_mode();
                }
                state_ = State::CHECK_WIFI_ALLOWED;
                break;

            case State::REBOOT_CYW43:
                // This state is only reached on DHCP timeout (15s) in WAITING_FOR_IP —
                // i.e. WiFi link came up but the router never assigned an IP. That is
                // extremely rare in normal use. A lighter alternative would be to go
                // straight back to CONNECTING or WIFI_POWERING_UP instead of tearing
                // down the CYW43 hardware entirely; a full reset is heavy-handed for
                // what is essentially "try again."
                //
                // WARNING: cyw43_arch_deinit() followed by cyw43_arch_init() causes
                // lwip_init() → memp_init() to run again, which rebuilds pool free lists
                // from scratch. Any live lwIP allocations (e.g. the httpd listening PCB)
                // will be corrupted. This is harmless only as long as MEMP_MEM_MALLOC=1
                // routes all pool allocations through malloc instead of the pool arrays.
                // If MEMP_MEM_MALLOC is ever disabled, this path must be reworked.
                printf("WiFiMgr: Hard resetting CYW43 chip...\n");
                if (ap_mode_active_) {
                    g_captive_portal_active = false;
                    dns_server_deinit();
                    dhcp_server_deinit(&ap_dhcp_server_);
                    ap_mode_active_ = false;
                }
                cyw43_arch_deinit();
                vTaskDelay(pdMS_TO_TICKS(500));
                state_ = State::UNINITIALIZED;
                fail_count = 0;
                break;

            case State::AP_STARTING:
            {
                // Ensure STA mode is off before starting AP (handles the CONNECTING
                // → AP_STARTING fallback path where STA was enabled but never connected)
                cyw43_arch_disable_sta_mode();

                // Resolve AP SSID: use configured value or derive from MAC
                if (g_flash_config.ap_ssid[0] != '\0') {
                    strncpy(ap_ssid_resolved_, g_flash_config.ap_ssid,
                            sizeof(ap_ssid_resolved_) - 1);
                } else {
                    uint8_t mac[6];
                    cyw43_hal_get_mac(CYW43_HAL_MAC_WLAN0, mac);
                    snprintf(ap_ssid_resolved_, sizeof(ap_ssid_resolved_),
                             "umod4_%02X%02X", mac[4], mac[5]);
                }
                ap_ssid_resolved_[sizeof(ap_ssid_resolved_) - 1] = '\0';

                // Resolve AP password: always use SSID as fallback so there is only
                // one default password regardless of how the device was set up.
                const char* ap_pw = (g_flash_config.ap_password[0] != '\0')
                                    ? g_flash_config.ap_password
                                    : ap_ssid_resolved_;

                printf("WiFiMgr: Starting AP mode SSID='%s'\n", ap_ssid_resolved_);
                cyw43_arch_enable_ap_mode(ap_ssid_resolved_, ap_pw, CYW43_AUTH_WPA2_AES_PSK);

                // Start DHCP server using the SDK's default AP IP (192.168.4.1)
                ip4_addr_t ap_ip, ap_mask;
                ap_ip.addr  = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
                ap_mask.addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);
                dhcp_server_init(&ap_dhcp_server_,
                                 (ip_addr_t*)&ap_ip,
                                 (ip_addr_t*)&ap_mask);
                dns_server_init((ip_addr_t*)&ap_ip);
                ap_mode_active_ = true;
                g_captive_portal_active = true;

                printf("WiFiMgr: AP active — connect to '%s', captive portal on 192.168.4.1\n",
                       ap_ssid_resolved_);
                state_ = State::AP_ACTIVE;
                break;
            }

            case State::AP_ACTIVE:
            {
                // Shut down radio if bike is moving
                if (gps_ != nullptr && gps_->getSpeedMph() > 20.0f) {
                    printf("WiFiMgr: Speed > 20 MPH — shutting down radio\n");
                    state_ = State::RADIO_OFF;
                    break;
                }

                // Slow-blink the CYW43 LED to indicate AP mode (1Hz toggle)
                static bool ap_led_state = false;
                ap_led_state = !ap_led_state;
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, ap_led_state);

                // Trigger a scan if countdown expired or API/user requested one
                if (--scan_countdown_ <= 0 || g_wifi_scan_results.scan_requested) {
                    state_ = State::AP_SCANNING;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                break;
            }

            case State::AP_SCANNING:
            {
                performScan();
                if (home_ssid_found_) {
                    printf("WiFiMgr: Home network found — switching to STA\n");
                    state_ = State::DISCONNECTING;
                } else {
                    printf("WiFiMgr: Home network not found\n");
                    scan_countdown_ = 60;
                    state_ = State::AP_ACTIVE;
                }
                break;
            }

            case State::RADIO_OFF:
            {
                // Tear down AP if still active
                if (ap_mode_active_) {
                    g_captive_portal_active = false;
                    dns_server_deinit();
                    dhcp_server_deinit(&ap_dhcp_server_);
                    cyw43_arch_disable_ap_mode();
                    ap_mode_active_ = false;
                    printf("WiFiMgr: AP mode stopped\n");
                }
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
                printf("WiFiMgr: Radio off — power cycle to resume\n");
                // Stay here until power cycle
                while (true) {
                    vTaskDelay(pdMS_TO_TICKS(10000));
                }
                break;
            }

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

void WiFiManager::setCredentials(const char* ssid, const char* password)
{
    if (ssid) {
        strncpy(wifiSsid_, ssid, sizeof(wifiSsid_) - 1);
        wifiSsid_[sizeof(wifiSsid_) - 1] = '\0';
    }
    if (password) {
        strncpy(wifiPassword_, password, sizeof(wifiPassword_) - 1);
        wifiPassword_[sizeof(wifiPassword_) - 1] = '\0';
    }
    printf("WiFiMgr: Credentials set (SSID='%s')\n", wifiSsid_);
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

    // Resolve address before acquiring the lwIP lock.
    // netconn_gethostbyname() posts to the tcpip thread and blocks waiting for
    // the reply — calling it while holding the lwIP lock would deadlock because
    // the tcpip thread can't run while we hold the mutex.
    ip_addr_t server_addr;

    if (!ip4addr_aton(serverHostname_, &server_addr)) {
        // Not a literal IP, resolve via DNS/mDNS
        printf("WiFiMgr: Not a literal IP, resolving via DNS...\n");

        err_t dns_err = netconn_gethostbyname(serverHostname_, &server_addr);
        if (dns_err != ERR_OK) {
            printf("WiFiMgr: Failed to resolve %s (err=%d)\n", serverHostname_, dns_err);
            return;
        }

        printf("WiFiMgr: Resolved %s to %s\n", serverHostname_, ip4addr_ntoa(&server_addr));
    } else {
        printf("WiFiMgr: Using literal IP address: %s\n", ip4addr_ntoa(&server_addr));
    }

    printf("WiFiMgr: Sending check-in to %s:%u\n", ip4addr_ntoa(&server_addr), serverPort_);
    printf("WiFiMgr: Payload: %s\n", payload);

    // All lwIP raw API calls must be made with the lwIP lock held.
    // This function is called from the WiFiMgr task and from the FreeRTOS timer
    // daemon task (heartbeatTimerCallback) — neither is the lwIP tcpip thread,
    // so cyw43_arch_lwip_begin()/end() is required to prevent racing with the
    // tcpip thread and corrupting lwIP's internal memory structures.
    cyw43_arch_lwip_begin();

    struct udp_pcb* pcb = udp_new();
    if (!pcb) {
        printf("WiFiMgr: Failed to create UDP PCB\n");
        cyw43_arch_lwip_end();
        return;
    }

    // Allocate pbuf for payload
    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, strlen(payload), PBUF_RAM);
    if (!p) {
        printf("WiFiMgr: Failed to allocate pbuf\n");
        udp_remove(pcb);
        cyw43_arch_lwip_end();
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

    cyw43_arch_lwip_end();
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
