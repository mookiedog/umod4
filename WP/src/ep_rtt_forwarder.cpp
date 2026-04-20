#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "SEGGER_RTT.h"
#include "ep_rtt_forwarder.h"
#include "ep_info.h"
#include "Swd.h"
#include "swd_lock.h"

// -------------------------------------------------------------------------
// SEGGER_RTT_BUFFER_UP layout (24 bytes per channel, host reads from pBuffer)
//   +0  sName       (uint32_t — pointer, ignored)
//   +4  pBuffer     (uint32_t — pointer to ring buffer in EP SRAM)
//   +8  SizeOfBuffer(uint32_t)
//   +12 WrOff       (uint32_t)
//   +16 RdOff       (uint32_t)
//   +20 Flags       (uint32_t)
//
// SEGGER_RTT_CB layout:
//   +0  acID[16]    (16 bytes)
//   +16 MaxNumUpBuffers   (uint32_t)
//   +20 MaxNumDownBuffers (uint32_t)
//   +24 aUp[0]      first SEGGER_RTT_BUFFER_UP (24 bytes)
//   +48 aUp[1]      second SEGGER_RTT_BUFFER_UP
// -------------------------------------------------------------------------

#define RTT_CB_UP0_OFFSET   24u     // offset of aUp[0] in SEGGER_RTT_CB
#define RTT_BUF_UP_SIZE     24u     // sizeof SEGGER_RTT_BUFFER_UP
#define RTT_BUF_PBUFFER_OFF  4u     // offset of pBuffer within BUFFER_UP
#define RTT_BUF_SIZEBUF_OFF  8u     // offset of SizeOfBuffer
#define RTT_BUF_WROFF_OFF   12u     // offset of WrOff
#define RTT_BUF_RDOFF_OFF   16u     // offset of RdOff

// Largest chunk we'll copy per channel per poll cycle (must be multiple of 4)
#define COPY_BUF_BYTES      256u

// Poll interval when connected and reading normally
#define POLL_INTERVAL_MS    100

// Back-off interval after connection failure or EpInfo not ready
#define BACKOFF_INTERVAL_MS 5000

// Forwarding state for one EP RTT channel
struct EpChanState {
    uint32_t buf_addr;      // pBuffer pointer in EP SRAM
    uint32_t buf_size;      // SizeOfBuffer
    uint32_t rd_off;        // our local copy of RdOff (tracks what we've consumed)
    int      wp_channel;    // target WP RTT channel index
};

static EpChanState s_chan[2];   // EP ch0→WP ch2, EP ch1→WP ch3
static bool        s_connected;

static char s_wp_vfy_buf[512];
static char s_ep_gen_buf[4096];
static char s_ep_vfy_buf[1024];

// -------------------------------------------------------------------------
// Read one BUFFER_UP struct fields we care about from EP via SWD.
// cb_addr: address of _SEGGER_RTT in EP SRAM.
// ch_idx:  EP channel index (0 or 1).
// Returns false if any SWD read fails.
static bool read_up_buf(uint32_t cb_addr, int ch_idx, EpChanState& out)
{
    // Read all 6 uint32_t fields of BUFFER_UP in one 24-byte read.
    // Address is 4-byte aligned (cb_addr is aligned, offsets are multiples of 4).
    uint32_t words[6];
    uint32_t up_addr = cb_addr + RTT_CB_UP0_OFFSET + (uint32_t)ch_idx * RTT_BUF_UP_SIZE;
    if (!swd->read_target_mem(up_addr, words, sizeof(words)))
        return false;

    out.buf_addr = words[RTT_BUF_PBUFFER_OFF / 4];
    out.buf_size = words[RTT_BUF_SIZEBUF_OFF / 4];
    out.rd_off   = words[RTT_BUF_RDOFF_OFF   / 4];
    // WrOff at index 3 is read later per-cycle, not stored here
    return (out.buf_addr != 0 && out.buf_size != 0);
}

// -------------------------------------------------------------------------
// Forward any new bytes from one EP RTT channel to the corresponding WP
// RTT channel.  Returns false on SWD error.
static bool forward_channel(EpChanState& ch, uint32_t cb_addr, int ch_idx)
{
    // Read WrOff from EP (single aligned 4-byte read)
    uint32_t wr_off_addr = cb_addr + RTT_CB_UP0_OFFSET
                           + (uint32_t)ch_idx * RTT_BUF_UP_SIZE
                           + RTT_BUF_WROFF_OFF;
    uint32_t wr_off;
    if (!swd->read_target_mem(wr_off_addr, &wr_off, 4))
        return false;

    if (wr_off == ch.rd_off)
        return true;    // nothing new

    // Determine how many bytes are available (ring buffer)
    uint32_t avail;
    if (wr_off > ch.rd_off)
        avail = wr_off - ch.rd_off;
    else
        avail = ch.buf_size - ch.rd_off + wr_off;  // wrap

    // Cap to COPY_BUF_BYTES
    if (avail > COPY_BUF_BYTES)
        avail = COPY_BUF_BYTES;

    // Handle wrap: if the contiguous bytes from rd_off to end-of-buffer
    // are fewer than avail, split into two reads.
    uint32_t contiguous = ch.buf_size - ch.rd_off;

    static uint8_t tmp[COPY_BUF_BYTES + 4];    // +4 for alignment rounding

    if (avail <= contiguous) {
        // No wrap needed for this chunk
        uint32_t src = ch.buf_addr + ch.rd_off;
        // SWD requires 4-byte aligned address; buf_addr and rd_off are both
        // ring-buffer-aligned (SEGGER guarantees power-of-2 sizes and writes
        // that maintain alignment, but rd_off may not be 4-aligned).
        // Read from the 4-byte-aligned address at or before src.
        uint32_t align_off = src & 3u;
        uint32_t aligned_src = src - align_off;
        uint32_t total_read = (align_off + avail + 3u) & ~3u;
        if (!swd->read_target_mem(aligned_src, (uint32_t*)(void*)tmp, total_read))
            return false;
        SEGGER_RTT_Write(ch.wp_channel, tmp + align_off, avail);
    } else {
        // Wrapping: read from rd_off to end of buffer, then 0 to remainder
        uint32_t first_len = contiguous;
        uint32_t second_len = avail - first_len;

        uint32_t src1 = ch.buf_addr + ch.rd_off;
        uint32_t align_off1 = src1 & 3u;
        uint32_t aligned_src1 = src1 - align_off1;
        uint32_t total_read1 = (align_off1 + first_len + 3u) & ~3u;
        if (!swd->read_target_mem(aligned_src1, (uint32_t*)(void*)tmp, total_read1))
            return false;
        SEGGER_RTT_Write(ch.wp_channel, tmp + align_off1, first_len);

        // Second segment starts at buf_addr (beginning of ring buffer)
        uint32_t src2 = ch.buf_addr;
        uint32_t total_read2 = (second_len + 3u) & ~3u;
        if (!swd->read_target_mem(src2, (uint32_t*)(void*)tmp, total_read2))
            return false;
        SEGGER_RTT_Write(ch.wp_channel, tmp, second_len);
    }

    // Advance RdOff in EP
    uint32_t new_rd_off = (ch.rd_off + avail) % ch.buf_size;
    uint32_t rd_off_addr = cb_addr + RTT_CB_UP0_OFFSET
                           + (uint32_t)ch_idx * RTT_BUF_UP_SIZE
                           + RTT_BUF_RDOFF_OFF;
    if (!swd->write_target_mem(rd_off_addr, &new_rd_off, 4)) {
        printf("ep_rtt_fwd: ch%d RdOff write failed\n", ch_idx);
        return false;
    }

    ch.rd_off = new_rd_off;
    return true;
}

// -------------------------------------------------------------------------
static void ep_rtt_forwarder_task(void*)
{
    s_connected = false;
    bool inhibit_msg_sent = false;

    for (;;) {
        // --- Steps 1-3: Connect and read descriptors (hold mutex for full setup) ---
        xSemaphoreTakeRecursive(g_swd_mutex, portMAX_DELAY);

        bool setup_ok = false;
        uint32_t rtt_cb_addr = 0;

        // Step 1: Connect to EP core 0, no halt
        if (!swd->connect_target(0, false)) {
            if (!inhibit_msg_sent) {
                const char* msg = "EP RTT unavailable: SWD inhibited (SPARE2 grounded)\n";
                SEGGER_RTT_WriteString(WP_RTT_CH_EP_GEN, msg);
                SEGGER_RTT_WriteString(WP_RTT_CH_EP_VFY, msg);
                inhibit_msg_sent = true;
            }
            s_connected = false;
        } else {
            inhibit_msg_sent = false;

            // Step 2: Read EpInfo from USB DPSRAM
            uint32_t ep_info_words[2];
            if (!swd->read_target_mem(EP_INFO_ADDR, ep_info_words, sizeof(ep_info_words))) {
                printf("ep_rtt_fwd: EpInfo read failed at 0x%08x\n", EP_INFO_ADDR);
                s_connected = false;
            } else {
                uint32_t magic = ep_info_words[0];
                rtt_cb_addr    = ep_info_words[1];

                if (magic != EP_INFO_MAGIC) {
                    printf("ep_rtt_fwd: waiting for EP (magic=0x%08x, want 0x%08x)\n",
                           magic, EP_INFO_MAGIC);
                } else {
                    // Step 3: Read BUFFER_UP descriptors
                    s_chan[0].wp_channel = WP_RTT_CH_EP_GEN;
                    s_chan[1].wp_channel = WP_RTT_CH_EP_VFY;
                    setup_ok = true;
                    for (int i = 0; i < 2 && setup_ok; i++) {
                        EpChanState fresh;
                        fresh.wp_channel = s_chan[i].wp_channel;
                        if (!read_up_buf(rtt_cb_addr, i, fresh)) {
                            printf("ep_rtt_fwd: BUFFER_UP read failed (cb=0x%08x)\n", rtt_cb_addr);
                            setup_ok = false;
                            break;
                        }
                        if (!s_connected) {
                            s_chan[i] = fresh;
                        } else {
                            s_chan[i].buf_addr = fresh.buf_addr;
                            s_chan[i].buf_size = fresh.buf_size;
                        }
                    }
                }
            }
        }

        xSemaphoreGiveRecursive(g_swd_mutex);

        if (!setup_ok) {
            s_connected = false;
            vTaskDelay(pdMS_TO_TICKS(BACKOFF_INTERVAL_MS));
            continue;
        }

        printf("ep_rtt_fwd: connected cb=0x%08x\n"
               "  ch0: buf=0x%08x size=%u rdoff=%u\n"
               "  ch1: buf=0x%08x size=%u rdoff=%u\n",
               rtt_cb_addr,
               s_chan[0].buf_addr, s_chan[0].buf_size, s_chan[0].rd_off,
               s_chan[1].buf_addr, s_chan[1].buf_size, s_chan[1].rd_off);
        s_connected = true;

        // --- Step 4: Poll loop — one lock per poll cycle, delay outside the lock ---
        for (;;) {
            xSemaphoreTakeRecursive(g_swd_mutex, portMAX_DELAY);
            bool poll_ok = true;
            for (int i = 0; i < 2 && poll_ok; i++) {
                if (!forward_channel(s_chan[i], rtt_cb_addr, i))
                    poll_ok = false;
            }
            xSemaphoreGiveRecursive(g_swd_mutex);

            if (!poll_ok) {
                printf("ep_rtt_fwd: SWD error during poll, reconnecting\n");
                s_connected = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
        }

        vTaskDelay(pdMS_TO_TICKS(BACKOFF_INTERVAL_MS));
    }
}

// -------------------------------------------------------------------------
void ep_rtt_channels_init(void)
{
    // Call this before vTaskStartScheduler() so Cortex-Debug sees all channels
    // with valid buffers on its initial RTT scan.
    SEGGER_RTT_ConfigUpBuffer(WP_RTT_CH_VFY,    "WP_VFY", s_wp_vfy_buf, sizeof(s_wp_vfy_buf),
                              SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    SEGGER_RTT_ConfigUpBuffer(WP_RTT_CH_EP_GEN, "EP_GEN", s_ep_gen_buf, sizeof(s_ep_gen_buf),
                              SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    SEGGER_RTT_ConfigUpBuffer(WP_RTT_CH_EP_VFY, "EP_VFY", s_ep_vfy_buf, sizeof(s_ep_vfy_buf),
                              SEGGER_RTT_MODE_NO_BLOCK_SKIP);
}

// -------------------------------------------------------------------------
void ep_rtt_forwarder_init(void)
{
    xTaskCreate(ep_rtt_forwarder_task, "ep_rtt_fwd",
                512,    // stack words
                nullptr, tskIDLE_PRIORITY + 1, nullptr);
}
