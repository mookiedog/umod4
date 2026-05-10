/*
 * Captive portal HTTPS intercept for umod4 WP.
 *
 * Android's captive portal detector probes https://connectivitycheck.gstatic.com/generate_204.
 * DNS resolves that hostname to 192.168.4.1 (our AP IP). Android then opens a TCP
 * connection to port 443. Since we have no TLS, we respond immediately with a plain
 * HTTP 302 redirect. Android's NetworkMonitor accepts this as proof of a captive
 * portal and shows the "Sign in to network" notification.
 *
 * We send the redirect immediately on connection accept, before receiving any data.
 * The TLS ClientHello (if it arrives) is discarded. This is non-standard but is the
 * correct approach for embedded captive portals and is what Android expects.
 */

#include "tcp443_server.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include <stdio.h>
#include <string.h>

static struct tcp_pcb *s_listen_pcb = NULL;

static const char kRedirect[] =
    "HTTP/1.1 302 Found\r\n"
    "Location: http://192.168.4.1/wifi_config.html\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n"
    "\r\n";

static err_t conn_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)arg;
    (void)len;
    tcp_close(pcb);
    return ERR_OK;
}

static err_t conn_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    (void)err;
    if (p) {
        tcp_recved(pcb, p->tot_len);
        pbuf_free(p);
    }
    return ERR_OK;
}

static void conn_err_cb(void *arg, err_t err)
{
    (void)arg;
    (void)err;
}

static err_t accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || newpcb == NULL) return ERR_VAL;

    tcp_setprio(newpcb, TCP_PRIO_MIN);
    tcp_recv(newpcb, conn_recv_cb);
    tcp_sent(newpcb, conn_sent_cb);
    tcp_err(newpcb, conn_err_cb);

    err_t e = tcp_write(newpcb, kRedirect, sizeof(kRedirect) - 1, TCP_WRITE_FLAG_COPY);
    if (e != ERR_OK) {
        tcp_close(newpcb);
        return ERR_OK;
    }
    tcp_output(newpcb);
    return ERR_OK;
}

void tcp443_server_init(void)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        printf("TCP443: Failed to allocate PCB\n");
        return;
    }

    err_t err = tcp_bind(pcb, IP_ANY_TYPE, 443);
    if (err != ERR_OK) {
        printf("TCP443: Failed to bind port 443 (err=%d)\n", err);
        tcp_close(pcb);
        return;
    }

    /* tcp_listen() consumes pcb and returns a new listen PCB (or NULL on OOM) */
    s_listen_pcb = tcp_listen(pcb);
    if (!s_listen_pcb) {
        printf("TCP443: tcp_listen failed\n");
        return;
    }

    tcp_accept(s_listen_pcb, accept_cb);
    printf("TCP443: Captive portal HTTPS intercept active on port 443\n");
}

void tcp443_server_deinit(void)
{
    if (s_listen_pcb) {
        tcp_close(s_listen_pcb);
        s_listen_pcb = NULL;
        printf("TCP443: Server stopped\n");
    }
}
