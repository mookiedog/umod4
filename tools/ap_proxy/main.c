#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

// ============================================================
// ap_proxy — USB CDC-ACM serial HTTP proxy for umod4 AP mode
//
// Protocol (line-based, newline-terminated):
//
//   SCAN                        -> OK [ssid1 ssid2 ...]
//   CONNECT <ssid> [password]   -> OK connected <ip>    (password defaults to ssid)
//   STATUS                      -> OK connected <ssid> <ip>  |  OK disconnected
//   GET <path>                  -> OK <http_status> <body>
//   POST <path> <json_body>     -> OK <http_status> <body>
//   DISCONNECT                  -> OK
//   PING                        -> OK
//
//   On error: ERR <message>
// ============================================================

#define AP_IP           "192.168.4.1"
#define AP_PORT         80
#define SSID_MAX        33
#define MAX_SCAN_SSIDS  8
#define HTTP_BUF_SIZE   4096
#define REQ_BUF_SIZE    1536
#define CMD_LINE_MAX    1024

// ---- WiFi scan -----------------------------------------------------------------

typedef struct {
    char ssids[MAX_SCAN_SSIDS][SSID_MAX];
    int  count;
} scan_state_t;

static scan_state_t g_scan;

static int scan_cb(void *env, const cyw43_ev_scan_result_t *result)
{
    scan_state_t *st = (scan_state_t *)env;
    if (result && st->count < MAX_SCAN_SSIDS) {
        if (strncmp((const char *)result->ssid, "umod4_", 6) == 0) {
            for (int i = 0; i < st->count; i++) {
                if (strncmp(st->ssids[i], (const char *)result->ssid, SSID_MAX) == 0)
                    return 0;
            }
            strncpy(st->ssids[st->count], (const char *)result->ssid, SSID_MAX - 1);
            st->ssids[st->count][SSID_MAX - 1] = '\0';
            st->count++;
        }
    }
    return 0;
}

static void cmd_scan(void)
{
    memset(&g_scan, 0, sizeof(g_scan));
    cyw43_wifi_scan_options_t opts = {0};

    cyw43_arch_lwip_begin();
    int err = cyw43_wifi_scan(&cyw43_state, &opts, &g_scan, scan_cb);
    cyw43_arch_lwip_end();

    if (err != 0) {
        printf("ERR scan failed %d\n", err);
        return;
    }

    absolute_time_t deadline = make_timeout_time_ms(8000);
    while (cyw43_wifi_scan_active(&cyw43_state) && !time_reached(deadline)) {
        sleep_ms(10);
    }

    // Give the radio time to settle after multi-channel scan before the
    // caller issues a CONNECT — without this delay, CYW43 returns LINK_FAIL.
    sleep_ms(500);

    printf("OK");
    for (int i = 0; i < g_scan.count; i++) {
        printf(" %s", g_scan.ssids[i]);
    }
    printf("\n");
}

// ---- WiFi connect / disconnect -------------------------------------------------

static bool g_connected = false;
static char  g_connected_ssid[SSID_MAX];

static void cmd_connect(const char *ssid, const char *password)
{
    if (g_connected) {
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        g_connected = false;
    }

    int err = cyw43_arch_wifi_connect_timeout_ms(
        ssid, password, CYW43_AUTH_WPA2_AES_PSK, 20000);
    if (err != 0) {
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);  // reset STA so scan works after failure
        printf("ERR connect failed %d\n", err);
        return;
    }

    g_connected = true;
    strncpy(g_connected_ssid, ssid, SSID_MAX - 1);
    g_connected_ssid[SSID_MAX - 1] = '\0';

    char ip_str[16];
    ip4addr_ntoa_r(netif_ip4_addr(netif_default), ip_str, sizeof(ip_str));
    printf("OK connected %s\n", ip_str);
}

static void cmd_disconnect(void)
{
    if (g_connected) {
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        g_connected = false;
        g_connected_ssid[0] = '\0';
    }
    printf("OK\n");
}

static void cmd_status(void)
{
    if (!g_connected) {
        printf("OK disconnected\n");
        return;
    }
    char ip_str[16];
    ip4addr_ntoa_r(netif_ip4_addr(netif_default), ip_str, sizeof(ip_str));
    printf("OK connected %s %s\n", g_connected_ssid, ip_str);
}

// ---- HTTP client (raw lwIP TCP, callback-based) --------------------------------

typedef struct {
    struct tcp_pcb *pcb;
    volatile bool   done;
    int             http_status;
    char            buf[HTTP_BUF_SIZE];
    int             buf_len;
    const char     *request;
    int             request_len;
} http_ctx_t;

static http_ctx_t g_http;  // static — commands are serialised, one at a time

static void http_close_pcb(http_ctx_t *ctx)
{
    if (ctx->pcb) {
        tcp_arg(ctx->pcb, NULL);
        tcp_recv(ctx->pcb, NULL);
        tcp_sent(ctx->pcb, NULL);
        tcp_err(ctx->pcb, NULL);
        tcp_close(ctx->pcb);
        ctx->pcb = NULL;
    }
}

static err_t http_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    http_ctx_t *ctx = (http_ctx_t *)arg;
    if (!p) {
        // Server closed — we have the full response
        ctx->buf[ctx->buf_len] = '\0';
        sscanf(ctx->buf, "HTTP/%*d.%*d %d", &ctx->http_status);
        http_close_pcb(ctx);
        ctx->done = true;
        return ERR_OK;
    }
    for (struct pbuf *q = p; q; q = q->next) {
        int space = HTTP_BUF_SIZE - 1 - ctx->buf_len;
        int copy  = (int)q->len < space ? (int)q->len : space;
        if (copy > 0) {
            memcpy(ctx->buf + ctx->buf_len, q->payload, copy);
            ctx->buf_len += copy;
        }
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t http_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err)
{
    http_ctx_t *ctx = (http_ctx_t *)arg;
    if (err != ERR_OK) {
        ctx->http_status = -1;
        ctx->done = true;
        return err;
    }
    tcp_write(pcb, ctx->request, ctx->request_len, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
    return ERR_OK;
}

static void http_err_cb(void *arg, err_t err)
{
    (void)err;
    http_ctx_t *ctx = (http_ctx_t *)arg;
    ctx->pcb = NULL;  // already freed by lwIP on error
    ctx->http_status = -1;
    ctx->done = true;
}

// Returns 0 on success; body_out points into g_http.buf (valid until next call).
static int do_http(const char *request_str, int *status_out, const char **body_out)
{
    memset(&g_http, 0, sizeof(g_http));
    g_http.request     = request_str;
    g_http.request_len = (int)strlen(request_str);

    cyw43_arch_lwip_begin();
    g_http.pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!g_http.pcb) {
        cyw43_arch_lwip_end();
        return -1;
    }
    tcp_arg(g_http.pcb, &g_http);
    tcp_recv(g_http.pcb, http_recv_cb);
    tcp_err(g_http.pcb, http_err_cb);

    ip_addr_t addr;
    IP4_ADDR(&addr, 192, 168, 4, 1);
    err_t e = tcp_connect(g_http.pcb, &addr, AP_PORT, http_connected_cb);
    cyw43_arch_lwip_end();

    if (e != ERR_OK) {
        return -1;
    }

    // CYW43 + lwIP are driven by the background IRQ; just sleep until done.
    absolute_time_t deadline = make_timeout_time_ms(10000);
    while (!g_http.done && !time_reached(deadline)) {
        sleep_ms(10);
    }

    if (!g_http.done) {
        cyw43_arch_lwip_begin();
        if (g_http.pcb) {
            tcp_abort(g_http.pcb);
            g_http.pcb = NULL;
        }
        cyw43_arch_lwip_end();
        return -1;
    }

    if (g_http.http_status <= 0) {
        return -1;
    }

    *status_out = g_http.http_status;
    char *body = strstr(g_http.buf, "\r\n\r\n");
    *body_out  = body ? (body + 4) : "";
    return 0;
}

static char g_req_buf[REQ_BUF_SIZE];

static void print_body(int status, const char *body)
{
    printf("OK %d ", status);
    for (const char *p = body; *p; p++) {
        if (*p != '\r' && *p != '\n')
            putchar(*p);
    }
    putchar('\n');
}

static void cmd_get(const char *path)
{
    snprintf(g_req_buf, sizeof(g_req_buf),
        "GET %s HTTP/1.0\r\n"
        "Host: " AP_IP "\r\n"
        "Connection: close\r\n"
        "\r\n",
        path);
    int status = 0;
    const char *body = NULL;
    if (do_http(g_req_buf, &status, &body) != 0) {
        printf("ERR request failed\n");
        return;
    }
    print_body(status, body);
}

static void cmd_post(const char *path, const char *json_body)
{
    int body_len = (int)strlen(json_body);
    snprintf(g_req_buf, sizeof(g_req_buf),
        "POST %s HTTP/1.0\r\n"
        "Host: " AP_IP "\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        path, body_len, json_body);
    int status = 0;
    const char *body = NULL;
    if (do_http(g_req_buf, &status, &body) != 0) {
        printf("ERR request failed\n");
        return;
    }
    print_body(status, body);
}


// ---- LED control ---------------------------------------------------------------
//
// When idle, LED blinks in a recognizable heartbeat fashion.
// Every time a command is received via USB, the LED gets turned on solid
// for the next 2 seconds. This means that 2 seconds after the last command
// is received, the LED reverts back to heartbeat mode.

static absolute_time_t g_led_solid_until;  // zero-init = already in the past
static absolute_time_t g_led_next_update;

static void led_kick(void)
{
    g_led_solid_until = make_timeout_time_ms(2000);
}

static void led_update(void)
{
    if (!time_reached(g_led_next_update))
        return;
    g_led_next_update = make_timeout_time_ms(5);

    bool on;
    if (!time_reached(g_led_solid_until)) {
        on = true;
    } else {
        uint32_t phase_ms = (uint32_t)(to_us_since_boot(get_absolute_time()) / 1000 % 1500);
        on = (phase_ms < 75) || (phase_ms >= 200 && phase_ms < 400);
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}

// ---- Command line parser -------------------------------------------------------

static char g_line[CMD_LINE_MAX];
static int  g_line_len = 0;

static void process_line(char *line)
{
    led_kick();
    // Trim trailing whitespace
    int len = (int)strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' '))
        line[--len] = '\0';
    if (len == 0)
        return;

    char *cmd = strtok(line, " ");
    if (!cmd)
        return;

    if (strcmp(cmd, "SCAN") == 0) {
        cmd_scan();
    } else if (strcmp(cmd, "CONNECT") == 0) {
        char *ssid = strtok(NULL, " ");
        char *pw   = strtok(NULL, " ");
        if (!ssid) { printf("ERR missing ssid\n"); return; }
        cmd_connect(ssid, pw ? pw : ssid);  // password defaults to ssid
    } else if (strcmp(cmd, "STATUS") == 0) {
        cmd_status();
    } else if (strcmp(cmd, "GET") == 0) {
        char *path = strtok(NULL, " ");
        if (!path) { printf("ERR missing path\n"); return; }
        cmd_get(path);
    } else if (strcmp(cmd, "POST") == 0) {
        char *path = strtok(NULL, " ");
        char *body = strtok(NULL, "");  // rest of line
        if (!path) { printf("ERR missing path\n"); return; }
        if (!body) { printf("ERR missing body\n"); return; }
        while (*body == ' ') body++;  // skip leading space
        cmd_post(path, body);
    } else if (strcmp(cmd, "DISCONNECT") == 0) {
        cmd_disconnect();
    } else if (strcmp(cmd, "PING") == 0) {
        printf("OK\n");
    } else if (strcmp(cmd, "BOOTSEL") == 0) {
        printf("OK rebooting to BOOTSEL\n");
        stdio_flush();
        sleep_ms(100);
        reset_usb_boot(0, 0);
    } else {
        printf("ERR unknown: %s\n", cmd);
    }
}

// ---- RX ring buffer (filled by chars_available callback in IRQ context) --------
//
// USB CDC suspends when idle; tud_ready() returns false during suspend so
// getchar_timeout_us() returns nothing. The chars_available callback fires
// from the USB IRQ exactly when data arrives — the bus is provably active
// at that moment — so getchar works. We buffer chars here; main loop processes.

#define RX_BUF_SIZE (CMD_LINE_MAX * 2)
static volatile char g_rxbuf[RX_BUF_SIZE];
static volatile int  g_rx_write = 0;
static volatile int  g_rx_read  = 0;

static void chars_available_cb(void *param)
{
    (void)param;
    int c;
    while ((c = getchar_timeout_us(0)) >= 0) {
        int next = (g_rx_write + 1) % RX_BUF_SIZE;
        if (next != g_rx_read) {
            g_rxbuf[g_rx_write] = (char)c;
            g_rx_write = next;
        }
    }
}

// ---- Main ----------------------------------------------------------------------

int main(void)
{
    stdio_init_all();

    if (cyw43_arch_init()) {
        while (true)
            tight_loop_contents();
    }
    cyw43_arch_enable_sta_mode();

    stdio_set_chars_available_callback(chars_available_cb, NULL);

    printf("READY\n");

    while (true) {
        led_update();
        if (g_rx_read == g_rx_write) {
            sleep_ms(1);
            continue;
        }
        char c = g_rxbuf[g_rx_read];
        g_rx_read = (g_rx_read + 1) % RX_BUF_SIZE;
        if (c == '\n' || c == '\r') {
            g_line[g_line_len] = '\0';
            if (g_line_len > 0)
                process_line(g_line);
            g_line_len = 0;
        } else if (g_line_len < CMD_LINE_MAX - 1) {
            g_line[g_line_len++] = (char)c;
        }
    }
}
