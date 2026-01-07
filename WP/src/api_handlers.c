#include "api_handlers.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

// External references
extern const char* get_wp_version(void);
extern bool wifi_is_connected(void);
extern const char* wifi_get_ssid(void);

// Buffer for JSON responses (reused across requests to save stack space)
static char json_response_buffer[512];

/**
 * CGI handler for /api/info endpoint.
 * Returns JSON with device status information.
 */
const char* cgi_api_info(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    (void)iIndex;
    (void)iNumParams;
    (void)pcParam;
    (void)pcValue;

    // Get device MAC address
    char mac_str[18] = "unknown";
    uint8_t mac[6];
    cyw43_hal_get_mac(CYW43_HAL_MAC_WLAN0, mac);
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Get uptime in seconds
    uint32_t uptime_seconds = xTaskGetTickCount() / configTICK_RATE_HZ;

    // Get WiFi status
    const char* wifi_status = wifi_is_connected() ? "true" : "false";
    const char* wifi_ssid = wifi_get_ssid();
    if (!wifi_ssid) wifi_ssid = "";

    // Build JSON response
    snprintf(json_response_buffer, sizeof(json_response_buffer),
             "{\n"
             "  \"device_mac\": \"%s\",\n"
             "  \"wp_version\": \"%s\",\n"
             "  \"uptime_seconds\": %lu,\n"
             "  \"wifi_connected\": %s,\n"
             "  \"wifi_ssid\": \"%s\"\n"
             "}",
             mac_str,
             get_wp_version(),
             (unsigned long)uptime_seconds,
             wifi_status,
             wifi_ssid);

    return json_response_buffer;
}

// CGI handler table
static const tCGI cgi_handlers[] = {
    {"/api/info", cgi_api_info},
};

void api_handlers_register(void)
{
    http_set_cgi_handlers(cgi_handlers, sizeof(cgi_handlers) / sizeof(tCGI));
}
