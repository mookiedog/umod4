#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// Common settings used in most of the pico_w examples
// (see https://www.nongnu.org/lwip/2_1_x/group__lwip__opts.html)

// We're using pico_cyw43_arch_lwip_sys_freertos with FreeRTOS
#define NO_SYS                      0
#define LWIP_SOCKET                 1
#define LWIP_NETCONN                1

// FreeRTOS thread and mailbox settings (required for NO_SYS=0)
#define TCPIP_THREAD_STACKSIZE      4096
#define TCPIP_THREAD_PRIO           3  // Priority 3 = TASK_HIGH_PRIORITY, below CYW43 (priority 4)
#define DEFAULT_THREAD_STACKSIZE    2048
#define DEFAULT_RAW_RECVMBOX_SIZE   8
#define DEFAULT_TCP_RECVMBOX_SIZE   8
#define DEFAULT_UDP_RECVMBOX_SIZE   8
#define TCPIP_MBOX_SIZE             16  // increased from 8: parallel page loads generate burst of ACKs

// Prevent lwIP from redefining struct timeval (already defined in newlib)
#define LWIP_TIMEVAL_PRIVATE        0

// Provide errno from newlib
#define LWIP_PROVIDE_ERRNO          0

// Use static memory instead of malloc. Malloc was causing fragmentation issues
// over time as well as making it hard to know how much RAM was really being used.

// MEM_LIBC_MALLOC = 0 (Use lwIP static pool)
// Prevents "sbrk ratchet" OOM. Previously, setting this to 1 caused malloc to
// permanently claim heap space for PBUF_RAM and httpd buffers (~16KB/transfer).
// Static allocation ensures the linker accounts for this memory and prevents
// runtime heap exhaustion. Ensure MEM_SIZE covers peak TCP_SND_BUF demand.
#define MEM_LIBC_MALLOC 0

// MEMP_MEM_MALLOC = 0 (Use lwIP fixed-size static arrays)
// Ensures zero fragmentation and predictable memory usage for PCBs and pbufs.
// NOTE: Requires cyw43_arch_init() (which calls memp_init()) to run BEFORE
// any service initialization (e.g., httpd_init()). Services are now deferred
// to NetworkManager::init_lwip_services() to satisfy this ordering.
// CAUTION: Re-initializing the CYW43 architecture may corrupt these live pools.
#define MEMP_MEM_MALLOC 0

#define MEM_ALIGNMENT               4
// MEM_SIZE: static pool for lwIP's mem_malloc (PBUF_RAM pbufs + hs->buf per connection).
// Peak demand = 2 connections × (TCP_SND_BUF for hs->buf + TCP_SND_BUF for PBUF_RAMs)
//             = 2 × (17520 + 17520) = ~70KB. 80KB gives comfortable headroom.
// TEST: increased from 48KB to match doubled TCP_SND_BUF.
#define MEM_SIZE                    81920
// MEMP_NUM_SYS_TIMEOUT: lwIP auto-calculates this from enabled features (TCP, ARP, DHCP×2,
// IGMP, DNS, IP_REASSEMBLY, mDNS×(1+MAX_SERVICES), netif_client_data) giving ~10.
// With MEMP_MEM_MALLOC=0, the pool is now a fixed static array so the auto value is just
// barely enough and mdns_resp_add_netif() overflows it. Set explicitly with headroom.
#define MEMP_NUM_SYS_TIMEOUT        20
#define MEMP_NUM_TCP_PCB            20  // Increased from 5→10→20 to handle connection churn
                                        // With TCP_MSL=1000ms (2s TIME_WAIT), 20 PCBs allows
                                        // 10 new connections/second sustained throughput
#define MEMP_NUM_TCP_SEG            64  // TCP_SND_QUEUELEN = ceil(4×17520/1460) = 49; 64 gives headroom
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              24
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_IGMP                   1
#define LWIP_RAW                    1
// TCP window and send buffer: 12×MSS = 17520 bytes.
// Must be large enough to hold one full chunk response (CHUNK_DOWNLOAD_MAX_SIZE + ~300 byte
// headers) in a single tcp_write to avoid multi-round sends that cause IncompleteRead errors.
// Peak mem_malloc demand = 2 × (17520 + 17520) = ~70KB, covered by MEM_SIZE=80KB.
// TEST: doubled from 6×MSS to measure whether WiFi throughput improves with larger window.
#define TCP_WND                     (12 * TCP_MSS)
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (12 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NUM_NETIF_CLIENT_DATA  1
#define MEM_STATS                   1  // debug: count mem_malloc failures
#define SYS_STATS                   0
#define MEMP_STATS                  1  // debug: count per-pool failures and track peak usage
#define LINK_STATS                  0
// #define ETH_PAD_SIZE                2
#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

// HTTP server configuration for MDL (Motorbike Data Link)
#define LWIP_HTTPD                      1
#define LWIP_HTTPD_CGI                  1
#define LWIP_HTTPD_SSI                  1
#define LWIP_HTTPD_CUSTOM_FILES         1
#define LWIP_HTTPD_FILE_EXTENSION       1
#define LWIP_HTTPD_DYNAMIC_FILE_READ    1
#define LWIP_HTTPD_DYNAMIC_HEADERS      1
#define LWIP_HTTPD_SUPPORT_POST         1
#define LWIP_HTTPD_SUPPORT_11_KEEPALIVE 1
#define LWIP_HTTPD_MAX_TAG_NAME_LEN     16
#define LWIP_HTTPD_MAX_TAG_INSERT_LEN   256
#define HTTPD_SERVER_PORT               80

// Use our custom-generated fsdata.c instead of SDK default
// This file is generated at build time from WP/www/ directory
// Note: This must be set via compile definition to get the correct build path
#ifndef HTTPD_FSDATA_FILE
#define HTTPD_FSDATA_FILE               "fsdata.c"
#endif

// TCP TIME_WAIT configuration
// TIME_WAIT duration = 2 * TCP_MSL (Maximum Segment Lifetime)
// Default TCP_MSL is 60000ms (60s), giving 120s TIME_WAIT - way too long!
// With 20 PCBs and connection reuse, 2s TIME_WAIT is aggressive but acceptable
// Reduced from default to prevent PCB pool exhaustion during uploads
#define TCP_MSL                         1000  // 1 second (TIME_WAIT = 2 seconds)

// HTTP server polling interval (X * 500ms)
// Reduced from 4 (2s) to 1 (500ms): the 2s value caused consistent ~1.8s TTFB
// delays when http_send() stalled (tcp_write ERR_MEM on pages > TCP_SND_BUF).
// With no bytes in flight, http_sent never fires; http_poll is the only recovery path.
#define HTTPD_POLL_INTERVAL             1

// Maximum retries before closing Keep-Alive connection
// With HTTPD_POLL_INTERVAL=1, each retry is 500ms, so 1 retry = ~500ms idle timeout.
// I have noticed that if HTTPD_MAX_RETRIES is 1, then firmware uploads quit working!
// 2 retries seems to be the minimum.
// Shorter idle window prevents browser/server FIN race on connection reuse:
// if the user navigates before the idle timer fires, the connection is still alive;
// if after, the connection is already cleanly closed so Chrome opens a fresh one.
#define HTTPD_MAX_RETRIES               2

// mDNS responder for device discovery (motorcycle.local)
#define LWIP_MDNS_RESPONDER             1
#define LWIP_MDNS_SEARCH                0  // Disable mDNS client/query (only need responder)
#define MDNS_MAX_SERVICES               1

#ifndef NDEBUG
#define LWIP_DEBUG                  1
#define LWIP_STATS                  1
#define LWIP_STATS_DISPLAY          0
#endif

#define ETHARP_DEBUG                LWIP_DBG_OFF
#define NETIF_DEBUG                 LWIP_DBG_OFF
#define PBUF_DEBUG                  LWIP_DBG_OFF
#define API_LIB_DEBUG               LWIP_DBG_OFF
#define API_MSG_DEBUG               LWIP_DBG_OFF
#define SOCKETS_DEBUG               LWIP_DBG_OFF
#define ICMP_DEBUG                  LWIP_DBG_OFF
#define INET_DEBUG                  LWIP_DBG_OFF
#define IP_DEBUG                    LWIP_DBG_OFF
#define IP_REASS_DEBUG              LWIP_DBG_OFF
#define RAW_DEBUG                   LWIP_DBG_OFF
#define MEM_DEBUG                   LWIP_DBG_OFF
#define MEMP_DEBUG                  LWIP_DBG_OFF
#define SYS_DEBUG                   LWIP_DBG_OFF
#define TCP_DEBUG                   LWIP_DBG_OFF
#define TCP_INPUT_DEBUG             LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG            LWIP_DBG_OFF
#define TCP_RTO_DEBUG               LWIP_DBG_OFF
#define TCP_CWND_DEBUG              LWIP_DBG_OFF
#define TCP_WND_DEBUG               LWIP_DBG_OFF
#define TCP_FR_DEBUG                LWIP_DBG_OFF
#define TCP_QLEN_DEBUG              LWIP_DBG_OFF
#define TCP_RST_DEBUG               LWIP_DBG_OFF
#define UDP_DEBUG                   LWIP_DBG_OFF
#define TCPIP_DEBUG                 LWIP_DBG_OFF
#define PPP_DEBUG                   LWIP_DBG_OFF
#define SLIP_DEBUG                  LWIP_DBG_OFF
#define DHCP_DEBUG                  LWIP_DBG_OFF
#define HTTPD_DEBUG                 LWIP_DBG_OFF

#endif /* __LWIPOPTS_H__ */
