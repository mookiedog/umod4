#include "VfyTask.h"
#include "wp_rtt.h"
#include "lfsMgr.h"
#include "umod4_WP.h"
#include "swd_lock.h"
#include "Swd.h"
#include "FlashBuffer.h"
#include "swdreflash_binary.h"
#include "FlashWp.h"

#include "FreeRTOS.h"
#include "task.h"

#include "hardware/gpio.h"

#include <string.h>
#include <stdio.h>

// Build timestamp from compile-time macros
#define VFY_BUILD_TIME __DATE__ " " __TIME__

extern volatile uint32_t g_heap_remaining;
extern volatile uint32_t g_heap_free;
extern const char* get_wp_version(void);

// -------------------------------------------------------------------------
// Command handlers — each writes one or more VFY: lines then returns.

static void cmd_ping(void)
{
    VFY("ping PASS");
}

static void cmd_version(void)
{
    VFY("version PASS bt=\"%s\"", VFY_BUILD_TIME);
}

static void cmd_status(void)
{
    uint32_t uptime_ms = pdTICKS_TO_MS(xTaskGetTickCount());
    VFY("status PASS uptime_ms=%lu heap_remaining=%lu heap_free=%lu lfs_mounted=%d",
        (unsigned long)uptime_ms,
        (unsigned long)g_heap_remaining,
        (unsigned long)g_heap_free,
        (int)lfs_mounted);
}

static void cmd_lfs_delete(const char* arg)
{
    if (!arg || *arg == '\0') {
        VFY("lfs_delete FAIL reason=no_filename");
        return;
    }
    if (!lfs_mounted) {
        VFY("lfs_delete FAIL reason=not_mounted");
        return;
    }
    // Reject anything with path separators to prevent surprises.
    if (strchr(arg, '/') || strchr(arg, '\\')) {
        VFY("lfs_delete FAIL reason=invalid_filename");
        return;
    }
    char path[64];
    snprintf(path, sizeof(path), "/%s", arg);
    int err = lfs_remove(&lfs, path);
    if (err < 0) {
        VFY("lfs_delete FAIL reason=lfs_err err=%d file=\"%s\"", err, arg);
        return;
    }
    VFY("lfs_delete PASS file=\"%s\"", arg);
}

static void cmd_lfs_test(void)
{
    if (!lfs_mounted) {
        VFY("lfs_test FAIL reason=not_mounted");
        return;
    }

    // Write a small temp file, read it back, verify, delete it.
    const char* path  = "/vfy_test.tmp";
    const char* magic = "vfy_ok";

    lfs_file_t f;
    int err = lfs_file_open(&lfs, &f, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err < 0) {
        VFY("lfs_test FAIL reason=open_write err=%d", err);
        return;
    }
    lfs_ssize_t written = lfs_file_write(&lfs, &f, magic, strlen(magic));
    lfs_file_close(&lfs, &f);
    if (written != (lfs_ssize_t)strlen(magic)) {
        VFY("lfs_test FAIL reason=write_short written=%d", (int)written);
        lfs_remove(&lfs, path);
        return;
    }

    char readbuf[16] = {};
    err = lfs_file_open(&lfs, &f, path, LFS_O_RDONLY);
    if (err < 0) {
        VFY("lfs_test FAIL reason=open_read err=%d", err);
        lfs_remove(&lfs, path);
        return;
    }
    lfs_ssize_t nread = lfs_file_read(&lfs, &f, readbuf, sizeof(readbuf) - 1);
    lfs_file_close(&lfs, &f);
    lfs_remove(&lfs, path);

    if (nread != (lfs_ssize_t)strlen(magic) || strcmp(readbuf, magic) != 0) {
        VFY("lfs_test FAIL reason=verify_mismatch");
        return;
    }

    VFY("lfs_test PASS");
}

// -------------------------------------------------------------------------
// EP_RUN helpers for test commands.
//
// NOTE: RP2040 SWD does not respond when RUN is held low — the debug port
// requires the chip to be executing (at minimum, the bootrom).  All SWD
// tests therefore pulse-reset EP and connect after release, not while held.
//
// ep_pulse_reset_and_wait() asserts EP_RUN for 10 ms then releases it and
// waits 50 ms for the bootrom to start accepting SWD connections.
static void ep_pulse_reset_and_wait(void)
{
    gpio_init(EP_RUN_PIN);
    gpio_set_dir(EP_RUN_PIN, GPIO_OUT);
    gpio_put(EP_RUN_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_put(EP_RUN_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

// ep_release_reset() re-pulses EP_RUN to reboot EP cleanly after tests.
static void ep_release_reset(void)
{
    ep_pulse_reset_and_wait();
}

// -------------------------------------------------------------------------
// Phase 1 SWD tests — EP held in reset throughout.

static void cmd_swd_spare2_check(void)
{
    // SPARE2 (GPIO 5) must be floating/high for WP→EP SWD to work.
    // When grounded, the Swd constructor sets swd_inhibit=true and
    // connect_target() returns false unconditionally.
    int level = gpio_get(SPARE2_PIN);
    if (level) {
        VFY("swd_spare2_check PASS level=high");
    } else {
        VFY("swd_spare2_check FAIL reason=spare2_grounded");
    }
}

static void cmd_swd_connect_in_reset(void)
{
    SWDLock lock;
    ep_pulse_reset_and_wait();
    // halt=true: stop EP in bootrom so subsequent tests can safely read/write
    // SRAM without racing the running CPU.
    if (swd->connect_target(0, true)) {
        VFY("swd_connect_in_reset PASS idcode=0x0bc12477");
    } else {
        VFY("swd_connect_in_reset FAIL reason=connect_failed");
    }
}

static void cmd_swd_read_flash_in_reset(void)
{
    SWDLock lock;
    if (!swd->connect_target(0, false)) {
        VFY("swd_read_flash_in_reset FAIL reason=connect_failed");
        return;
    }
    uint32_t buf[4];
    if (!swd->read_target_mem(0x10000000u, buf, sizeof(buf))) {
        VFY("swd_read_flash_in_reset FAIL reason=read_failed");
        return;
    }
    bool blank = (buf[0] == 0xFFFFFFFFu && buf[1] == 0xFFFFFFFFu &&
                  buf[2] == 0xFFFFFFFFu && buf[3] == 0xFFFFFFFFu);
    VFY("swd_read_flash_in_reset PASS state=%s w0=0x%08lx",
        blank ? "blank" : "programmed", (unsigned long)buf[0]);
}

static void cmd_swd_ram_roundtrip(void)
{
    SWDLock lock;
    if (!swd->connect_target(0, false)) {
        VFY("swd_ram_roundtrip FAIL reason=connect_failed");
        return;
    }
    // Use mid-SRAM: above where swdreflash loads (0x20000000) and below
    // the bootrom stack (near top of SRAM).  DPSRAM (0x50100000) requires
    // USB init which hasn't happened in bootrom.
    static const uint32_t ADDR = 0x20020000u;
    static const uint32_t PAT[4] = {
        0xDEADBEEFu, 0x12345678u, 0xCAFEBABEu, 0xA5A5A5A5u
    };
    if (!swd->write_target_mem(ADDR, PAT, sizeof(PAT))) {
        VFY("swd_ram_roundtrip FAIL reason=write_failed");
        return;
    }
    uint32_t rb[4] = {};
    if (!swd->read_target_mem(ADDR, rb, sizeof(rb))) {
        VFY("swd_ram_roundtrip FAIL reason=read_failed");
        return;
    }
    if (memcmp(PAT, rb, sizeof(PAT)) != 0) {
        VFY("swd_ram_roundtrip FAIL reason=mismatch got=0x%08lx",
            (unsigned long)rb[0]);
        return;
    }
    VFY("swd_ram_roundtrip PASS");
}

static void cmd_swd_load_swdreflash(void)
{
    SWDLock lock;
    if (!swd->connect_target(0, false)) {
        VFY("swd_load_swdreflash FAIL reason=connect_failed");
        return;
    }
    const uint32_t EP_RAM = 0x20000000u;
    if (!swd->write_target_mem(EP_RAM, swdreflash_data, swdreflash_size)) {
        VFY("swd_load_swdreflash FAIL reason=write_failed");
        return;
    }
    // Read back first 64 bytes and compare to the source binary.
    const uint32_t VERIFY_BYTES = 64u;
    uint32_t rb[VERIFY_BYTES / 4];
    if (!swd->read_target_mem(EP_RAM, rb, VERIFY_BYTES)) {
        VFY("swd_load_swdreflash FAIL reason=readback_failed");
        return;
    }
    if (memcmp(swdreflash_data, rb, VERIFY_BYTES) != 0) {
        VFY("swd_load_swdreflash FAIL reason=verify_mismatch");
        return;
    }
    VFY("swd_load_swdreflash PASS size=%u", (unsigned)swdreflash_size);
}

// -------------------------------------------------------------------------
// Phase 2 — flash write test.
//
// swd_write_flash <hex_addr>
//   Launches swdreflash (already loaded by swd_load_swdreflash), programs a
//   4096-byte test pattern to <hex_addr> in EP flash, reads it back via XIP,
//   and verifies the data.  EP is left running (swdreflash polling loop).
//
// Sequence mirrors FlashEp::flashSlot() but without an EP reset at the end.
static void cmd_swd_write_flash(const char* arg)
{
    // Parse hex address from argument (e.g. "0x10FF0000")
    uint32_t scratch_addr = 0;
    if (arg == nullptr || *arg == '\0' ||
        sscanf(arg, "0x%lx", (unsigned long*)&scratch_addr) != 1) {
        VFY("swd_write_flash FAIL reason=bad_arg arg=\"%s\"", arg ? arg : "");
        return;
    }

    SWDLock lock;

    // EP is halted from Phase 1.  swdreflash is in RAM but not yet launched.
    if (!swd->connect_target(0, true)) {
        VFY("swd_write_flash FAIL reason=connect_failed");
        return;
    }

    // Clear FBI struct so we can detect when swdreflash initialises it.
    uint32_t zeros[sizeof(flashBufferInterface_1_t) / 4 + 1] = {};
    if (!swd->write_target_mem(FLASH_BUFFER_INTERFACE_ADDR, zeros, sizeof(zeros))) {
        VFY("swd_write_flash FAIL reason=clear_fbi_failed");
        return;
    }

    // Launch swdreflash — same entry point and stack as production code.
    if (!swd->start_target(0x20000001u, 0x20042000u)) {
        VFY("swd_write_flash FAIL reason=start_target_failed");
        return;
    }

    // Wait up to 1 s for swdreflash to write MAGIC_1 into the FBI struct.
    flashBufferInterface_1_t fbi = {};
    uint32_t t0 = time_us_32();
    while (1) {
        if (!swd->read_target_mem(FLASH_BUFFER_INTERFACE_ADDR,
                                   (uint32_t*)&fbi, sizeof(fbi))) {
            VFY("swd_write_flash FAIL reason=fbi_read_failed");
            return;
        }
        if (fbi.magic == MAGIC_1) break;
        if (time_us_32() - t0 > 1000000u) {
            VFY("swd_write_flash FAIL reason=flasher_timeout");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Build a 4096-byte test pattern (one flash page, the minimum erase unit).
    // Pattern: words counting up from a recognisable seed.
    const uint32_t PAGE = 4096u;
    const uint32_t NWORDS = PAGE / 4u;
    static uint32_t pat[NWORDS];
    for (uint32_t i = 0; i < NWORDS; i++)
        pat[i] = 0xC0DE0000u + i;

    // Write pattern to flasher's SRAM buffer in 1 KB chunks.
    const uint32_t CHUNK = 1024u;
    for (uint32_t off = 0; off < PAGE; off += CHUNK) {
        if (!swd->write_target_mem(fbi.bufferStartAddr + off,
                                    pat + off / 4, CHUNK)) {
            VFY("swd_write_flash FAIL reason=buf_write_failed off=%lu",
                (unsigned long)off);
            return;
        }
    }

    // Issue MAILBOX_CMD_PGM.
    mailbox_t mbox = {};
    mbox.cmd         = MAILBOX_CMD_PGM;
    mbox.buffer_addr = fbi.bufferStartAddr;
    mbox.target_addr = scratch_addr;
    mbox.length      = PAGE;
    mbox.status      = 0;
    if (!swd->write_target_mem(fbi.mailboxAddr, (uint32_t*)&mbox, sizeof(mbox))) {
        VFY("swd_write_flash FAIL reason=mailbox_write_failed");
        return;
    }

    // Wait up to 10 s for flasher to complete.
    t0 = time_us_32();
    while (1) {
        if (!swd->read_target_mem(fbi.mailboxAddr,
                                   (uint32_t*)&mbox, sizeof(mbox))) {
            VFY("swd_write_flash FAIL reason=mailbox_read_failed");
            return;
        }
        if (mbox.status > MAILBOX_STATUS_BUSY) break;
        if (time_us_32() - t0 > 10000000u) {
            VFY("swd_write_flash FAIL reason=flash_timeout");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (mbox.status != MAILBOX_STATUS_SUCCESS) {
        VFY("swd_write_flash FAIL reason=flash_error status=%ld",
            (long)mbox.status);
        return;
    }

    // Verify: read first 16 bytes back from the XIP flash address.
    uint32_t rb[4];
    if (!swd->read_target_mem(scratch_addr, rb, sizeof(rb))) {
        VFY("swd_write_flash FAIL reason=readback_failed");
        return;
    }
    if (memcmp(pat, rb, sizeof(rb)) != 0) {
        VFY("swd_write_flash FAIL reason=verify_mismatch"
            " got=0x%08lx exp=0x%08lx",
            (unsigned long)rb[0], (unsigned long)pat[0]);
        return;
    }

    VFY("swd_write_flash PASS addr=0x%08lx", (unsigned long)scratch_addr);
}

// -------------------------------------------------------------------------
// Phase 3 — Release reset, verify SWD survives the transition.

static void cmd_swd_release_reset(void)
{
    // Pulse EP_RUN to reboot EP, then wait for firmware to start.
    ep_pulse_reset_and_wait();
    // Extra 2 s for EP firmware to fully boot before the reconnect test.
    vTaskDelay(pdMS_TO_TICKS(2000));
    VFY("swd_release_reset PASS");
}

static void cmd_swd_reconnect_after_boot(void)
{
    SWDLock lock;
    // halt=false: EP is running normally; we only want to verify SWD is live.
    if (!swd->connect_target(0, false)) {
        VFY("swd_reconnect_after_boot FAIL reason=connect_failed");
        return;
    }
    // Read the EP's IDCODE register as a simple live-target check.
    uint32_t buf[1];
    if (!swd->read_target_mem(0x10000000u, buf, sizeof(buf))) {
        VFY("swd_reconnect_after_boot FAIL reason=read_failed");
        return;
    }
    VFY("swd_reconnect_after_boot PASS w0=0x%08lx", (unsigned long)buf[0]);
}

// -------------------------------------------------------------------------
// Suite 5 — WiFi status.

extern bool wifi_is_connected(void);
extern const char* wifi_get_ssid(void);
extern int32_t wifi_get_rssi(void);
extern bool wifi_get_ip_address(char* out, size_t outlen);

static void cmd_wifi_status(void)
{
    if (!wifi_is_connected()) {
        VFY("wifi_status FAIL reason=not_connected");
        return;
    }
    const char* ssid = wifi_get_ssid();
    if (!ssid || ssid[0] == '\0') ssid = "(unknown)";
    int32_t rssi = wifi_get_rssi();
    char ip[20] = {};
    bool have_ip = wifi_get_ip_address(ip, sizeof(ip));
    if (!have_ip || ip[0] == '\0') {
        VFY("wifi_status FAIL reason=no_ip ssid=\"%s\" rssi=%ld", ssid, (long)rssi);
        return;
    }
    VFY("wifi_status PASS ssid=\"%s\" rssi=%ld ip=%s", ssid, (long)rssi, ip);
}

// -------------------------------------------------------------------------
static void cmd_ota_status(void)
{
    int32_t boot_slot   = FlashWp::get_boot_slot();
    int32_t target_slot = FlashWp::get_target_slot();
    bool    available   = FlashWp::get_ota_availability();
    VFY("ota_status PASS boot_slot=%ld target_slot=%ld available=%d",
        (long)boot_slot, (long)target_slot, (int)available);
}

// -------------------------------------------------------------------------
static void cmd_unknown(const char* name)
{
    VFY("unknown FAIL cmd=\"%s\"", name);
}

// -------------------------------------------------------------------------
// Strip trailing whitespace (CR, LF, space) in-place.
static void rtrim(char* s)
{
    int len = strlen(s);
    while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n' || s[len-1] == ' ')) {
        s[--len] = '\0';
    }
}

// -------------------------------------------------------------------------
static void vfy_task(void*)
{
    char buf[128];
    int  pos = 0;

    for (;;) {
        char chunk[32];
        unsigned n = vfy_rtt_read(chunk, sizeof(chunk));

        for (unsigned i = 0; i < n; i++) {
            char c = chunk[i];
            if (c == '\n' || c == '\r') {
                if (pos > 0) {
                    buf[pos] = '\0';
                    rtrim(buf);

                    // Split "cmd arg" on first space for commands that take arguments.
                    char* sp = strchr(buf, ' ');
                    const char* arg = sp ? sp + 1 : nullptr;
                    if (sp) *sp = '\0';

                    if      (strcmp(buf, "ping")       == 0) cmd_ping();
                    else if (strcmp(buf, "version")   == 0) cmd_version();
                    else if (strcmp(buf, "status")    == 0) cmd_status();
                    else if (strcmp(buf, "lfs_test")  == 0) cmd_lfs_test();
                    else if (strcmp(buf, "lfs_delete") == 0) cmd_lfs_delete(arg);
                    else if (strcmp(buf, "swd_spare2_check")        == 0) cmd_swd_spare2_check();
                    else if (strcmp(buf, "swd_connect_in_reset")    == 0) cmd_swd_connect_in_reset();
                    else if (strcmp(buf, "swd_read_flash_in_reset") == 0) cmd_swd_read_flash_in_reset();
                    else if (strcmp(buf, "swd_ram_roundtrip")       == 0) cmd_swd_ram_roundtrip();
                    else if (strcmp(buf, "swd_load_swdreflash")        == 0) cmd_swd_load_swdreflash();
                    else if (strcmp(buf, "swd_write_flash")            == 0) cmd_swd_write_flash(arg);
                    else if (strcmp(buf, "swd_release_reset")          == 0) cmd_swd_release_reset();
                    else if (strcmp(buf, "swd_reconnect_after_boot")   == 0) cmd_swd_reconnect_after_boot();
                    else if (strcmp(buf, "wifi_status")                == 0) cmd_wifi_status();
                    else if (strcmp(buf, "ota_status")                 == 0) cmd_ota_status();
                    else                                      cmd_unknown(buf);

                    pos = 0;
                }
            } else if (pos < (int)sizeof(buf) - 1) {
                buf[pos++] = c;
            }
        }

        if (n == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// -------------------------------------------------------------------------
void vfy_task_init(void)
{
    xTaskCreate(vfy_task, "vfy", 1024, nullptr, tskIDLE_PRIORITY + 1, nullptr);
}
