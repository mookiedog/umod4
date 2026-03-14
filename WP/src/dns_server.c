/*
 * Captive portal DNS server for umod4 WP.
 *
 * When in AP mode, resolves all DNS A-record queries to the device IP
 * (192.168.4.1). This causes phones and laptops to detect a captive portal
 * and show a "Sign in to network" notification, which opens wifi_config.html.
 *
 * Implementation uses a lwIP raw UDP PCB, matching the pattern in dhcpserver.c.
 * No lwIP DNS framework involvement — this is purely a responder.
 */

#include "dns_server.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include <string.h>
#include <stdio.h>

#define DNS_PORT 53

/* DNS header flag bits */
#define DNS_QR_RESPONSE     0x8000u     /* This is a response */
#define DNS_AA              0x0400u     /* Authoritative answer */
#define DNS_RA              0x0080u     /* Recursion available */
#define DNS_RCODE_OK        0x0000u
#define DNS_RCODE_NXDOMAIN  0x0003u

/* DNS record types */
#define DNS_TYPE_A      1u
#define DNS_TYPE_ANY    255u

/* Short TTL so clients re-query quickly after WiFi is configured */
#define DNS_CAPTIVE_TTL_S   10u

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_hdr_t;

static struct udp_pcb *s_pcb = NULL;
static ip_addr_t s_ap_ip;

/*
 * Skip a label-encoded DNS name.
 * Returns pointer to the byte immediately after the terminating zero (or
 * compression pointer), or NULL if the packet is truncated or malformed.
 */
static const uint8_t *skip_qname(const uint8_t *p, const uint8_t *end)
{
    while (p < end) {
        uint8_t len = *p;
        if (len == 0) {
            return p + 1;               /* end-of-name zero byte */
        }
        if ((len & 0xC0u) == 0xC0u) {  /* compression pointer */
            if (p + 2 > end) return NULL;
            return p + 2;
        }
        if ((len & 0xC0u) != 0) return NULL; /* reserved bits set: malformed */
        p += 1 + len;
    }
    return NULL;
}

static void dns_recv_cb(void *arg, struct udp_pcb *pcb,
                        struct pbuf *p, const ip_addr_t *src_addr, u16_t src_port)
{
    (void)arg;

    if (p->tot_len < sizeof(dns_hdr_t)) {
        pbuf_free(p);
        return;
    }

    /*
     * Static buffer: safe because the lwIP tcpip thread is single-threaded
     * and this callback is never re-entered. Avoids placing 512+ bytes on
     * the tcpip thread stack, which has limited headroom.
     * Buffer is large enough for query (≤512 bytes) plus a 16-byte A-record answer.
     */
    static uint8_t buf[512 + 16];

    uint16_t plen = (p->tot_len < 512u) ? p->tot_len : 512u;
    pbuf_copy_partial(p, buf, plen, 0);
    pbuf_free(p);

    dns_hdr_t *hdr = (dns_hdr_t *)buf;

    /* Only handle standard queries (QR=0, OPCODE=0) */
    uint16_t rx_flags = lwip_ntohs(hdr->flags);
    if ((rx_flags & 0x8000u) || ((rx_flags >> 11) & 0x0Fu) != 0) {
        return;
    }

    /* Find the end of the question section (QNAME + QTYPE + QCLASS) */
    const uint8_t *qstart = buf + sizeof(dns_hdr_t);
    const uint8_t *end    = buf + plen;

    /* Decode and log the queried hostname */
    {
        char name_buf[128];
        const uint8_t *np = qstart;
        size_t nlen = 0;
        while (np < end && *np != 0 && nlen < sizeof(name_buf) - 2) {
            uint8_t label_len = *np++;
            if ((label_len & 0xC0u) == 0xC0u) break;
            if (nlen > 0) name_buf[nlen++] = '.';
            size_t copy = label_len;
            if (nlen + copy >= sizeof(name_buf) - 1) copy = sizeof(name_buf) - nlen - 1;
            memcpy(name_buf + nlen, np, copy);
            nlen += copy;
            np += label_len;
        }
        name_buf[nlen] = '\0';
        printf("DNS: query '%s' from %s\n", name_buf, ipaddr_ntoa(src_addr));
    }
    const uint8_t *after_name = skip_qname(qstart, end);

    if (!after_name || after_name + 4 > end) {
        return; /* truncated or malformed */
    }

    uint16_t qtype = (uint16_t)((after_name[0] << 8) | after_name[1]);
    /* QCLASS at after_name[2..3] — accepted without checking */

    /* Response length so far: header + question section (including QTYPE + QCLASS) */
    size_t resp_len = (size_t)(after_name + 4 - buf);

    /* Modify the header in place for the response */
    hdr->qdcount = lwip_htons(1);
    hdr->nscount = 0;
    hdr->arcount = 0;

    if (qtype == DNS_TYPE_A || qtype == DNS_TYPE_ANY) {
        /* Append a single A record pointing to the AP IP */
        hdr->flags   = lwip_htons(DNS_QR_RESPONSE | DNS_AA | DNS_RA | DNS_RCODE_OK);
        hdr->ancount = lwip_htons(1);

        uint8_t *ans = buf + resp_len;  /* Points into the 16-byte headroom */

        /* NAME: compression pointer to offset 12 (start of question section) */
        *ans++ = 0xC0;
        *ans++ = 0x0C;

        /* TYPE: A */
        *ans++ = 0x00;
        *ans++ = DNS_TYPE_A;

        /* CLASS: IN */
        *ans++ = 0x00;
        *ans++ = 0x01;

        /* TTL (big-endian) */
        uint32_t ttl = DNS_CAPTIVE_TTL_S;
        *ans++ = (uint8_t)(ttl >> 24);
        *ans++ = (uint8_t)(ttl >> 16);
        *ans++ = (uint8_t)(ttl >>  8);
        *ans++ = (uint8_t)(ttl      );

        /* RDLENGTH: 4 */
        *ans++ = 0x00;
        *ans++ = 0x04;

        /* RDATA: AP IP (network byte order) */
        uint32_t ip_nbo = ip_2_ip4(&s_ap_ip)->addr;
        memcpy(ans, &ip_nbo, 4);
        ans += 4;

        resp_len = (size_t)(ans - buf);
    } else {
        /* NXDOMAIN for AAAA and everything else */
        hdr->flags   = lwip_htons(DNS_QR_RESPONSE | DNS_AA | DNS_RCODE_NXDOMAIN);
        hdr->ancount = 0;
    }

    struct pbuf *rp = pbuf_alloc(PBUF_TRANSPORT, (u16_t)resp_len, PBUF_RAM);
    if (rp) {
        memcpy(rp->payload, buf, resp_len);
        udp_sendto(pcb, rp, src_addr, src_port);
        pbuf_free(rp);
    }
}

void dns_server_init(const ip_addr_t *ap_ip)
{
    ip_addr_copy(s_ap_ip, *ap_ip);

    s_pcb = udp_new();
    if (!s_pcb) {
        printf("DNS: Failed to allocate UDP PCB\n");
        return;
    }

    udp_recv(s_pcb, dns_recv_cb, NULL);

    err_t err = udp_bind(s_pcb, IP_ANY_TYPE, DNS_PORT);
    if (err != ERR_OK) {
        printf("DNS: Failed to bind port 53 (err=%d)\n", err);
        udp_remove(s_pcb);
        s_pcb = NULL;
        return;
    }

    printf("DNS: Captive portal server started, all queries → %s\n", ipaddr_ntoa(ap_ip));
}

void dns_server_deinit(void)
{
    if (s_pcb) {
        udp_remove(s_pcb);
        s_pcb = NULL;
        printf("DNS: Server stopped\n");
    }
}
