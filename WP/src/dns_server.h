#pragma once

#include "lwip/ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the captive portal DNS server.
 * Listens on UDP port 53 and answers all A-record queries with ap_ip,
 * redirecting all hostname lookups to the device. This triggers the
 * "Sign in to network" captive portal notification on iOS, Android,
 * Windows, and macOS when in AP mode.
 *
 * @param ap_ip  The device's AP IP address (e.g. 192.168.4.1)
 */
void dns_server_init(const ip_addr_t *ap_ip);

/**
 * Stop the captive portal DNS server and release the UDP PCB.
 */
void dns_server_deinit(void);

#ifdef __cplusplus
}
#endif
