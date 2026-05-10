#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the captive portal HTTPS intercept on TCP port 443.
 *
 * Android's captive portal probe uses HTTPS (connectivitycheck.gstatic.com:443).
 * DNS resolves that hostname to 192.168.4.1, then Android opens a TCP connection
 * to port 443. We respond immediately with a plain HTTP 302 redirect (no TLS).
 * Android's NetworkMonitor accepts this as evidence of a captive portal and shows
 * the "Sign in to network" notification.
 *
 * Must be called from the lwIP tcpip thread (e.g. inside a tcpip_callback or
 * from a task that already holds the lwIP lock), matching dns_server_init().
 */
void tcp443_server_init(void);

/**
 * Stop the captive portal HTTPS intercept and release the TCP PCB.
 */
void tcp443_server_deinit(void);

#ifdef __cplusplus
}
#endif
