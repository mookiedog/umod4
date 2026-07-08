#include "VfyTask.h"
#include "wp_rtt.h"
#include "lfsMgr.h"
#include "LogStore.h"
#include "Logger.h"
#include "umod4_WP.h"
#include "swd_lock.h"
#include "Swd.h"
#include "FlashBuffer.h"
#include "swdreflash_binary.h"
#include "FlashWp.h"
#include "Gps.h"
#include "log_ids.h"

#include "FreeRTOS.h"
#include "task.h"

#include "hardware/gpio.h"

#include <string.h>
#include <stdio.h>
#include <limits.h>

extern uint16_t ecuLiveLog[256];
extern uint16_t ecuLogidRxCount[256];
extern const char* get_ecu_metadata_str(void);
extern bool        get_ecu_metadata_complete(void);
extern Gps* gps;

extern volatile uint32_t g_t1_oflo_prev_us;
extern volatile uint32_t g_t1_oflo_last_us;

#define VFY_BUILD_TIME __DATE__ " " __TIME__

extern volatile uint32_t g_heap_remaining;
extern volatile uint32_t g_heap_free;

extern bool        wifi_is_connected(void);
extern bool        wifi_is_ap_mode(void);
extern const char* wifi_get_ssid(void);
extern const char* wifi_get_ap_ssid(void);
extern int32_t     wifi_get_rssi(void);
extern bool        wifi_get_ip_address(char* out, size_t outlen);

// -------------------------------------------------------------------------
// SWD boot-time connectivity state — set once by swd_boot_check(), read by
// cmd_swd() and cmd_health().  States form a sequence: each value names the
// step that failed, or "ready" if all steps passed.
//
// Future: if a /api/status HTTP endpoint is added, the underlying data
// sources (gps->getHealth(), sdCard->state, lfs_mounted, lfs_reformatted,
// lfs_mount_ms, FlashWp statics, ecuLiveLog/ecuLogidRxCount) are all
// accessible to an HTTP handler directly.  The cmd_xxx() functions here are
// specific to the VFY RTT channel and should not be re-used for HTTP.

typedef enum {
    SWD_INHIBITED,       // SPARE2 grounded — WP→EP SWD disabled
    SWD_CONNECT_FAIL,    // could not connect to running EP
    SWD_ROUNDTRIP_FAIL,  // RAM write/read/verify failed
    SWD_READY,           // full check passed, SWD operational
} SwdBootState;

static SwdBootState s_swd_state = SWD_CONNECT_FAIL;

static const char* swd_state_str(SwdBootState s)
{
    switch (s) {
        case SWD_INHIBITED:      return "inhibited";
        case SWD_CONNECT_FAIL:   return "connect_fail";
        case SWD_ROUNDTRIP_FAIL: return "roundtrip_fail";
        case SWD_READY:          return "ready";
        default:                 return "unknown";
    }
}

// -------------------------------------------------------------------------
// SD card hw state string — mirrors SdCardBase::state_t

static const char* hw_state_str(SdCardBase::state_t s)
{
    switch (s) {
        case SdCardBase::NO_CARD:     return "no_card";
        case SdCardBase::MAYBE_CARD:  return "maybe_card";
        case SdCardBase::POWER_UP:    return "power_up";
        case SdCardBase::INIT_CARD:   return "init_card";
        case SdCardBase::VERIFYING:   return "verifying";
        case SdCardBase::OPERATIONAL: return "operational";
        default:                      return "unknown";
    }
}

// -------------------------------------------------------------------------
// GPS presence string

static const char* gps_presence_str(GpsPresence p)
{
    switch (p) {
        case GPS_NOT_PRESENT:   return "not_present";
        case GPS_MAYBE_PRESENT: return "pending";
        case GPS_PRESENT:       return "present";
        default:                return "unknown";
    }
}

// -------------------------------------------------------------------------
// emit_xxx() — write "key":{...} fragment, no outer braces, no newline.
// Used by both cmd_xxx() (which wraps in { }\n) and cmd_status().

static void emit_boot(void)
{
    vfy_printf("\"boot\":{\"slot\":%ld,\"target\":%ld,\"built\":\"%s\"}",
               (long)FlashWp::get_boot_slot(),
               (long)FlashWp::get_target_slot(),
               VFY_BUILD_TIME);
}

static void emit_sd(void)
{
    if (!sdCard) {
        vfy_printf("\"sd\":{\"state\":\"not_started\"}");
        return;
    }
    vfy_printf("\"sd\":{\"state\":\"%s\",\"size_mb\":%lu}",
               hw_state_str(sdCard->state),
               (unsigned long)sdCard->getCapacityMB());
}

static void emit_filesystem(void)
{
    vfy_printf("\"filesystem\":{\"state\":\"%s\",\"reformatted\":%s,\"mount_ms\":%lu}",
               lfs_mounted ? "mounted" : "not_mounted",
               lfs_reformatted ? "true" : "false",
               (unsigned long)lfs_mount_ms);
}

static void emit_gps(void)
{
    if (!gps) {
        vfy_printf("\"gps\":{\"state\":\"not_started\"}");
        return;
    }

    GpsHealth h;
    gps->getHealth(&h);

    const char* state;
    if (h.presence == GPS_NOT_PRESENT) {
        state = "not_present";
    } else if (h.presence == GPS_MAYBE_PRESENT) {
        state = "pending";
    } else if (h.nav_pvt_count == 0) {
        state = "pending";
    } else if (h.nav_pvt_age_ms < 2000) {
        state = "connected";
    } else {
        state = "stale";
    }

    int32_t tim_tp_age     = (h.tim_tp_age_ms     == UINT32_MAX) ? -1 : (int32_t)h.tim_tp_age_ms;
    int32_t nav_timels_age = (h.nav_timels_age_ms == UINT32_MAX) ? -1 : (int32_t)h.nav_timels_age_ms;
    int32_t nav_pvt_age    = (h.nav_pvt_age_ms    == UINT32_MAX) ? -1 : (int32_t)h.nav_pvt_age_ms;

    vfy_printf("\"gps\":{\"state\":\"%s\",\"present\":\"%s\","
               "\"rx_err\":%lu,\"cksum_err\":%lu,"
               "\"tim_tp\":%lu,\"nav_timels\":%lu,\"nav_pvt\":%lu,\"unknown\":%lu,"
               "\"tim_tp_age_ms\":%ld,\"nav_timels_age_ms\":%ld,\"nav_pvt_age_ms\":%ld}",
               state,
               gps_presence_str(h.presence),
               (unsigned long)h.rx_errors,
               (unsigned long)h.cksum_errors,
               (unsigned long)h.tim_tp_count,
               (unsigned long)h.nav_timels_count,
               (unsigned long)h.nav_pvt_count,
               (unsigned long)h.unknown_count,
               (long)tim_tp_age,
               (long)nav_timels_age,
               (long)nav_pvt_age);
}

static void emit_ecu(void)
{
    const char* meta = get_ecu_metadata_complete() ? get_ecu_metadata_str() : "null";
    uint32_t prev = g_t1_oflo_prev_us;
    uint32_t last = g_t1_oflo_last_us;
    uint32_t t1_period = (prev && last) ? (last - prev) : 0;
    vfy_printf("\"ecu\":{\"eclk_khz\":%u,\"meta\":%s,"
               "\"cpu_events\":%u,\"cpu_last\":%u,"
               "\"t1_oflo\":%u,\"t1_period_us\":%lu,"
               "\"vta_count\":%u,\"vm_count\":%u,"
               "\"tha_count\":%u,\"tha_last\":%u,"
               "\"aap_count\":%u,"
               "\"thw_count\":%u,\"thw_last\":%u,"
               "\"tp1_count\":%u,\"tp2_count\":%u,"
               "\"l000c_count\":%u,\"l000c_last\":%u}",
               (unsigned)ecuLiveLog[LOGID_EP_ECLK_KHZ_TYPE_U16],
               meta,
               (unsigned)ecuLogidRxCount[LOGID_ECU_CPU_EVENT_TYPE_U8],
               (unsigned)ecuLiveLog[LOGID_ECU_CPU_EVENT_TYPE_U8],
               (unsigned)ecuLogidRxCount[LOGID_ECU_T1_OFLO_TYPE_TS],
               (unsigned long)t1_period,
               (unsigned)ecuLogidRxCount[LOGID_ECU_RAW_VTA_TYPE_U16],
               (unsigned)ecuLogidRxCount[LOGID_ECU_RAW_VM_TYPE_U8],
               (unsigned)ecuLogidRxCount[LOGID_ECU_RAW_THA_TYPE_U8],
               (unsigned)ecuLiveLog[LOGID_ECU_RAW_THA_TYPE_U8],
               (unsigned)ecuLogidRxCount[LOGID_ECU_RAW_AAP_TYPE_U8],
               (unsigned)ecuLogidRxCount[LOGID_ECU_RAW_THW_TYPE_U8],
               (unsigned)ecuLiveLog[LOGID_ECU_RAW_THW_TYPE_U8],
               (unsigned)ecuLogidRxCount[LOGID_ECU_TP_CO1_RAW_TYPE_U8],
               (unsigned)ecuLogidRxCount[LOGID_ECU_TP_CO2_RAW_TYPE_U8],
               (unsigned)ecuLogidRxCount[LOGID_ECU_ECU_ERROR_L000C_TYPE_U8],
               (unsigned)ecuLiveLog[LOGID_ECU_ECU_ERROR_L000C_TYPE_U8]);
}

static void emit_swd(void)
{
    vfy_printf("\"swd\":{\"state\":\"%s\"}", swd_state_str(s_swd_state));
}

static void emit_wifi(void)
{
    if (wifi_is_ap_mode()) {
        const char* ssid = wifi_get_ap_ssid();
        vfy_printf("\"wifi\":{\"state\":\"ap_mode\",\"ssid\":\"%s\"}",
                   ssid ? ssid : "");
        return;
    }
    if (!wifi_is_connected()) {
        vfy_printf("\"wifi\":{\"state\":\"not_connected\"}");
        return;
    }
    const char* ssid = wifi_get_ssid();
    int32_t rssi = wifi_get_rssi();
    char ip[20] = {};
    wifi_get_ip_address(ip, sizeof(ip));
    vfy_printf("\"wifi\":{\"state\":\"connected\",\"ssid\":\"%s\",\"rssi\":%ld,\"ip\":\"%s\"}",
               ssid ? ssid : "", (long)rssi, ip);
}

static void emit_heap(void)
{
    vfy_printf("\"heap\":{\"remaining\":%lu,\"free\":%lu}",
               (unsigned long)g_heap_remaining,
               (unsigned long)g_heap_free);
}

// -------------------------------------------------------------------------
// Status command handlers — each wraps its emit_xxx() in { }\n.

static void cmd_boot(void)      { vfy_printf("{"); emit_boot();       vfy_printf("}\n"); }
static void cmd_sd(void)        { vfy_printf("{"); emit_sd();         vfy_printf("}\n"); }
static void cmd_filesystem(void){ vfy_printf("{"); emit_filesystem(); vfy_printf("}\n"); }
static void cmd_gps(void)       { vfy_printf("{"); emit_gps();        vfy_printf("}\n"); }
static void cmd_ecu(void)       { vfy_printf("{"); emit_ecu();        vfy_printf("}\n"); }
static void cmd_swd(void)        { vfy_printf("{"); emit_swd();        vfy_printf("}\n"); }
static void cmd_wifi(void)       { vfy_printf("{"); emit_wifi();       vfy_printf("}\n"); }
static void cmd_heap(void)       { vfy_printf("{"); emit_heap();       vfy_printf("}\n"); }

// -------------------------------------------------------------------------
// Status aggregator — sequential emit of all status commands.

static void cmd_status(void)
{
    vfy_printf("{\"status\":{");
    emit_boot();       vfy_printf(",");
    emit_sd();         vfy_printf(",");
    emit_filesystem(); vfy_printf(",");
    emit_gps();        vfy_printf(",");
    emit_ecu();        vfy_printf(",");
    emit_swd();        vfy_printf(",");
    emit_wifi();       vfy_printf(",");
    emit_heap();
    vfy_printf("}}\n");
}

// -------------------------------------------------------------------------
// Operation command handlers.

static void cmd_filesystem_test_rw(void)
{
    if (!lfs_mounted) {
        vfy_printf("{\"filesystem_test_rw\":{\"state\":\"not_mounted\"}}\n");
        return;
    }

    const char* path  = "/vfy_test.tmp";
    const char* magic = "vfy_ok";

    lfs_file_t f;
    int err = lfs_file_open(&lfs, &f, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err < 0) {
        vfy_printf("{\"filesystem_test_rw\":{\"state\":\"open_write_failed\",\"err\":%d}}\n", err);
        return;
    }
    lfs_ssize_t written = lfs_file_write(&lfs, &f, magic, strlen(magic));
    lfs_file_close(&lfs, &f);
    if (written != (lfs_ssize_t)strlen(magic)) {
        vfy_printf("{\"filesystem_test_rw\":{\"state\":\"write_failed\",\"written\":%d}}\n", (int)written);
        lfs_remove(&lfs, path);
        return;
    }

    char readbuf[16] = {};
    err = lfs_file_open(&lfs, &f, path, LFS_O_RDONLY);
    if (err < 0) {
        vfy_printf("{\"filesystem_test_rw\":{\"state\":\"open_read_failed\",\"err\":%d}}\n", err);
        lfs_remove(&lfs, path);
        return;
    }
    lfs_ssize_t nread = lfs_file_read(&lfs, &f, readbuf, sizeof(readbuf) - 1);
    lfs_file_close(&lfs, &f);
    lfs_remove(&lfs, path);

    if (nread != (lfs_ssize_t)strlen(magic) || strcmp(readbuf, magic) != 0) {
        vfy_printf("{\"filesystem_test_rw\":{\"state\":\"verify_failed\"}}\n");
        return;
    }

    vfy_printf("{\"filesystem_test_rw\":{\"state\":\"ok\"}}\n");
}

static void cmd_filesystem_test_delete(const char* arg)
{
    if (!arg || *arg == '\0') {
        vfy_printf("{\"filesystem_test_delete\":{\"state\":\"no_filename\"}}\n");
        return;
    }
    if (!lfs_mounted) {
        vfy_printf("{\"filesystem_test_delete\":{\"state\":\"not_mounted\"}}\n");
        return;
    }
    if (strchr(arg, '/') || strchr(arg, '\\')) {
        vfy_printf("{\"filesystem_test_delete\":{\"state\":\"invalid_filename\"}}\n");
        return;
    }

    // Route log_N.um4 to LogStore
    if (logStore && strncmp(arg, "log_", 4) == 0) {
        const char* p = arg + 4;
        char* end;
        unsigned long n = strtoul(p, &end, 10);
        if (end != p && strcmp(end, ".um4") == 0) {
            bool ok = logStore->deleteLog((uint32_t)n);
            vfy_printf("{\"filesystem_test_delete\":{\"state\":\"%s\",\"file\":\"%s\"}}\n",
                       ok ? "ok" : "error", arg);
            return;
        }
    }

    // LFS file
    char path[64];
    snprintf(path, sizeof(path), "/%s", arg);
    int err = lfs_remove(&lfs, path);
    if (err < 0) {
        vfy_printf("{\"filesystem_test_delete\":{\"state\":\"error\",\"err\":%d,\"file\":\"%s\"}}\n", err, arg);
        return;
    }
    vfy_printf("{\"filesystem_test_delete\":{\"state\":\"ok\",\"file\":\"%s\"}}\n", arg);
}

static void cmd_logger_stop(void)
{
    if (!logger) {
        vfy_printf("{\"logger_stop\":{\"state\":\"not_initialized\"}}\n");
        return;
    }
    if (logStore && logStore->getActiveLogNumber() >= 0)
        logStore->closeActiveLog();
    logger->deinit();
    vfy_printf("{\"logger_stop\":{\"state\":\"ok\"}}\n");
}

static void cmd_logstore_test_chunk_crossing(void)
{
    if (!logStore || !logger) {
        vfy_printf("{\"logstore_test_chunk_crossing\":{\"state\":\"not_initialized\"}}\n");
        return;
    }

    const char* fail_reason = nullptr;
    uint32_t chunk_bytes = logStore->getChunkBytes();

    // Stop the Logger
    if (logStore->getActiveLogNumber() >= 0)
        logStore->closeActiveLog();
    logger->deinit();

    uint32_t baseline_free = logStore->getFreeChunks();

    int32_t log_num = logStore->createLog();
    if (log_num < 0) { fail_reason = "createLog failed"; goto done; }

    {
        uint8_t buf[512];

        // Write 0xAA at start of chunk A (offset 0)
        memset(buf, 0xAA, 512);
        if (logStore->write(buf, 512) != 512) {
            fail_reason = "write start of A failed"; goto cleanup;
        }

        // Seek near end of chunk A, write 0xBB to fill it, 0xCC crosses into B
        if (!logStore->seek(chunk_bytes - 512)) {
            fail_reason = "seek end of A failed"; goto cleanup;
        }
        memset(buf, 0xBB, 512);
        if (logStore->write(buf, 512) != 512) {
            fail_reason = "write end of A failed"; goto cleanup;
        }
        memset(buf, 0xCC, 512);
        if (logStore->write(buf, 512) != 512) {
            fail_reason = "write start of B failed"; goto cleanup;
        }

        // Seek near end of chunk B — seek() allocates chunk B if needed
        if (!logStore->seek(2 * chunk_bytes - 512)) {
            fail_reason = "seek end of B failed"; goto cleanup;
        }
        memset(buf, 0xDD, 512);
        if (logStore->write(buf, 512) != 512) {
            fail_reason = "write end of B failed"; goto cleanup;
        }
        memset(buf, 0xEE, 512);
        if (logStore->write(buf, 512) != 512) {
            fail_reason = "write start of C failed"; goto cleanup;
        }
        logStore->syncMetadata();

        // Verify 3 chunks allocated
        LogStoreLogInfo info;
        if (!logStore->getLogInfo((uint32_t)log_num, &info)) {
            fail_reason = "getLogInfo failed"; goto cleanup;
        }
        if (info.num_chunks != 3) {
            fail_reason = "expected 3 chunks"; goto cleanup;
        }
        if (logStore->getFreeChunks() != baseline_free - 3) {
            fail_reason = "free count wrong after extends"; goto cleanup;
        }

        // Read back and verify all 5 written sectors
        struct { uint32_t offset; uint8_t expected; const char* label; } checks[] = {
            { 0,                      0xAA, "start of A" },
            { chunk_bytes - 512,      0xBB, "end of A" },
            { chunk_bytes,            0xCC, "start of B" },
            { 2 * chunk_bytes - 512,  0xDD, "end of B" },
            { 2 * chunk_bytes,        0xEE, "start of C" },
        };
        for (auto& c : checks) {
            memset(buf, 0, 512);
            if (logStore->readLog((uint32_t)log_num, c.offset, buf, 512) != 512) {
                fail_reason = c.label; goto cleanup;
            }
            for (int i = 0; i < 512; i++) {
                if (buf[i] != c.expected) {
                    fail_reason = c.label; goto cleanup;
                }
            }
        }
    }

cleanup:
    if (logStore->getActiveLogNumber() >= 0)
        logStore->closeActiveLog();
    if (log_num >= 0)
        logStore->deleteLog((uint32_t)log_num);
    if (!fail_reason && logStore->getFreeChunks() != baseline_free)
        fail_reason = "free count not restored after delete";

done:
    logStore->init(&lfs, sdCard);
    logger->init(&lfs);

    if (fail_reason) {
        printf("LogStore: chunk crossing test FAILED: %s\n", fail_reason);
        vfy_printf("{\"logstore_test_chunk_crossing\":{\"state\":\"fail\",\"reason\":\"%s\"}}\n",
                   fail_reason);
    } else {
        printf("LogStore: chunk crossing test PASSED\n");
        vfy_printf("{\"logstore_test_chunk_crossing\":{\"state\":\"pass\"}}\n");
    }
}

static void cmd_logger_start(void)
{
    if (!logger || !logStore) {
        vfy_printf("{\"logger_start\":{\"state\":\"not_initialized\"}}\n");
        return;
    }
    if (!lfs_mounted) {
        vfy_printf("{\"logger_start\":{\"state\":\"not_mounted\"}}\n");
        return;
    }
    logStore->init(&lfs, sdCard);
    logger->init(&lfs);
    vfy_printf("{\"logger_start\":{\"state\":\"ok\",\"next_log\":%ld}}\n",
               (long)(logStore->getActiveLogNumber() + 1));
}

static void cmd_logstore_fsck(void)
{
    if (!logStore) {
        vfy_printf("{\"logstore_fsck\":{\"state\":\"not_initialized\"}}\n");
        return;
    }
    uint32_t errors = logStore->verify();
    vfy_printf("{\"logstore_fsck\":{\"state\":\"%s\",\"errors\":%lu}}\n",
               errors == 0 ? "pass" : "fail",
               (unsigned long)errors);
}

// Write a crafted .meta file to LFS for FSCK testing.
// Arg format: "log_num chunk1[,chunk2,...] offset total [active]"
// Example: "900 0 1000 1000" — log 900, chunk 0, triggers chunk-0 error
// Example: "902 5 1000 1000 active" — log 902, chunk 5, marked active
static void cmd_logstore_write_test_meta(const char* arg)
{
    if (!arg || *arg == '\0' || !lfs_mounted) {
        vfy_printf("{\"logstore_write_test_meta\":{\"state\":\"%s\"}}\n",
                   !lfs_mounted ? "not_mounted" : "no_arg");
        return;
    }

    uint32_t log_num = 0;
    char chunks_str[64] = {};
    uint32_t offset = 0;
    uint32_t total = 0;
    char active_str[8] = {};

    int n = sscanf(arg, "%lu %63s %lu %lu %7s",
                   (unsigned long*)&log_num,
                   chunks_str,
                   (unsigned long*)&offset,
                   (unsigned long*)&total,
                   active_str);
    if (n < 4) {
        vfy_printf("{\"logstore_write_test_meta\":{\"state\":\"bad_args\"}}\n");
        return;
    }

    bool active = (n >= 5 && strcmp(active_str, "active") == 0);

    // Build JSON
    char json[256];
    int pos = snprintf(json, sizeof(json),
        "{\"log\":%lu,\"active\":%s,\"chunks\":[%s],\"offset\":%lu,\"total\":%lu}",
        (unsigned long)log_num,
        active ? "true" : "false",
        chunks_str,
        (unsigned long)offset,
        (unsigned long)total);

    // Write to LFS
    char path[32];
    snprintf(path, sizeof(path), "/log_t%lu.meta", (unsigned long)log_num);

    lfs_file_t f;
    int err = lfs_file_open(&lfs, &f, path, LFS_O_CREAT | LFS_O_TRUNC | LFS_O_WRONLY);
    if (err < 0) {
        vfy_printf("{\"logstore_write_test_meta\":{\"state\":\"open_error\",\"err\":%d}}\n", err);
        return;
    }
    lfs_file_write(&lfs, &f, json, pos);
    lfs_file_close(&lfs, &f);

    vfy_printf("{\"logstore_write_test_meta\":{\"state\":\"ok\",\"file\":\"%s\",\"json\":%s}}\n",
               path, json);
}

// -------------------------------------------------------------------------
// EP_RUN helpers.

static void ep_pulse_reset_and_wait(void)
{
    gpio_init(EP_RUN_PIN);
    gpio_set_dir(EP_RUN_PIN, GPIO_OUT);
    gpio_put(EP_RUN_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_put(EP_RUN_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

// -------------------------------------------------------------------------
// swd_test_flash — full SWD flash programming cycle in a single command.
// Proves the flash mechanism works end-to-end: load flasher stub to EP SRAM,
// write a 4K test pattern to the scratch block, release EP from reset, then
// re-run the SWD connectivity check to confirm EP booted cleanly.
// The scratch address (last 64K of UNUSED_FLASH) is passed as the argument.

static void cmd_swd_test_flash(const char* arg)
{
    uint32_t scratch_addr = 0;
    if (arg == nullptr || *arg == '\0' ||
        sscanf(arg, "0x%lx", (unsigned long*)&scratch_addr) != 1) {
        vfy_printf("{\"swd_test_flash\":{\"state\":\"bad_arg\",\"arg\":\"%s\"}}\n",
                   arg ? arg : "");
        return;
    }

    // Phase 1: reset EP, halt it in bootrom, load flasher to SRAM.
    // Must reset first so core 1 is not running during flash_exit_xip().
    ep_pulse_reset_and_wait();
    {
        SWDLock lock;
        if (!swd->connect_target(0, true)) {
            vfy_printf("{\"swd_test_flash\":{\"state\":\"load_connect_failed\"}}\n");
            return;
        }
        const uint32_t EP_RAM = 0x20000000u;
        if (!swd->write_target_mem(EP_RAM, swdreflash_data, swdreflash_size)) {
            vfy_printf("{\"swd_test_flash\":{\"state\":\"load_write_failed\"}}\n");
            return;
        }
        const uint32_t VERIFY_BYTES = 64u;
        uint32_t rb[VERIFY_BYTES / 4];
        if (!swd->read_target_mem(EP_RAM, rb, VERIFY_BYTES) ||
            memcmp(swdreflash_data, rb, VERIFY_BYTES) != 0) {
            vfy_printf("{\"swd_test_flash\":{\"state\":\"load_verify_failed\"}}\n");
            return;
        }
    }

    // Phase 2: launch flasher (connect with reset), write scratch block.
    {
        SWDLock lock;

        if (!swd->connect_target(0, true)) {
            vfy_printf("{\"swd_test_flash\":{\"state\":\"flash_connect_failed\"}}\n");
            return;
        }

        uint32_t zeros[sizeof(flashBufferInterface_1_t) / 4 + 1] = {};
        if (!swd->write_target_mem(FLASH_BUFFER_INTERFACE_ADDR, zeros, sizeof(zeros))) {
            vfy_printf("{\"swd_test_flash\":{\"state\":\"clear_fbi_failed\"}}\n");
            return;
        }

        if (!swd->start_target(0x20000001u, 0x20042000u)) {
            vfy_printf("{\"swd_test_flash\":{\"state\":\"start_failed\"}}\n");
            return;
        }

        flashBufferInterface_1_t fbi = {};
        uint32_t t0 = time_us_32();
        while (1) {
            if (!swd->read_target_mem(FLASH_BUFFER_INTERFACE_ADDR,
                                       (uint32_t*)&fbi, sizeof(fbi))) {
                vfy_printf("{\"swd_test_flash\":{\"state\":\"fbi_read_failed\"}}\n");
                return;
            }
            if (fbi.magic == MAGIC_1) break;
            if (time_us_32() - t0 > 1000000u) {
                vfy_printf("{\"swd_test_flash\":{\"state\":\"flasher_timeout\"}}\n");
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        const uint32_t PAGE = 4096u;
        const uint32_t NWORDS = PAGE / 4u;
        static uint32_t pat[NWORDS];
        for (uint32_t i = 0; i < NWORDS; i++)
            pat[i] = 0xC0DE0000u + i;

        const uint32_t CHUNK = 1024u;
        for (uint32_t off = 0; off < PAGE; off += CHUNK) {
            if (!swd->write_target_mem(fbi.bufferStartAddr + off,
                                        pat + off / 4, CHUNK)) {
                vfy_printf("{\"swd_test_flash\":{\"state\":\"buf_write_failed\",\"off\":%lu}}\n",
                           (unsigned long)off);
                return;
            }
        }

        mailbox_t mbox = {};
        mbox.cmd         = MAILBOX_CMD_PGM;
        mbox.buffer_addr = fbi.bufferStartAddr;
        mbox.target_addr = scratch_addr;
        mbox.length      = PAGE;
        mbox.status      = 0;
        if (!swd->write_target_mem(fbi.mailboxAddr, (uint32_t*)&mbox, sizeof(mbox))) {
            vfy_printf("{\"swd_test_flash\":{\"state\":\"mailbox_write_failed\"}}\n");
            return;
        }

        t0 = time_us_32();
        while (1) {
            if (!swd->read_target_mem(fbi.mailboxAddr,
                                       (uint32_t*)&mbox, sizeof(mbox))) {
                vfy_printf("{\"swd_test_flash\":{\"state\":\"mailbox_read_failed\"}}\n");
                return;
            }
            if (mbox.status > MAILBOX_STATUS_BUSY) break;
            if (time_us_32() - t0 > 10000000u) {
                vfy_printf("{\"swd_test_flash\":{\"state\":\"flash_timeout\"}}\n");
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (mbox.status != MAILBOX_STATUS_SUCCESS) {
            vfy_printf("{\"swd_test_flash\":{\"state\":\"flash_error\",\"status\":%ld}}\n",
                       (long)mbox.status);
            return;
        }

        uint32_t rb[4];
        if (!swd->read_target_mem(scratch_addr, rb, sizeof(rb))) {
            vfy_printf("{\"swd_test_flash\":{\"state\":\"readback_failed\"}}\n");
            return;
        }
        if (memcmp(pat, rb, sizeof(rb)) != 0) {
            vfy_printf("{\"swd_test_flash\":{\"state\":\"verify_failed\","
                       "\"got\":\"0x%08lx\",\"exp\":\"0x%08lx\"}}\n",
                       (unsigned long)rb[0], (unsigned long)pat[0]);
            return;
        }
    }

    // Phase 3: release EP from reset, wait for boot, re-check connectivity.
    ep_pulse_reset_and_wait();
    vTaskDelay(pdMS_TO_TICKS(2000));

    {
        SWDLock lock;
        if (!swd->connect_target(0, false)) {
            s_swd_state = SWD_CONNECT_FAIL;
            vfy_printf("{\"swd_test_flash\":{\"state\":\"ep_reconnect_failed\"}}\n");
            return;
        }
        static const uint32_t ADDR = 0x20040000u;  // SCRATCH_X — safe from Core 1 writes
        static const uint32_t PAT[4] = {
            0xDEADBEEFu, 0x12345678u, 0xCAFEBABEu, 0xA5A5A5A5u
        };
        uint32_t rb[4] = {};
        if (!swd->write_target_mem(ADDR, PAT, sizeof(PAT)) ||
            !swd->read_target_mem(ADDR, rb, sizeof(rb))    ||
            memcmp(PAT, rb, sizeof(PAT)) != 0) {
            s_swd_state = SWD_ROUNDTRIP_FAIL;
            vfy_printf("{\"swd_test_flash\":{\"state\":\"ep_roundtrip_failed\"}}\n");
            return;
        }
        s_swd_state = SWD_READY;
    }

    vfy_printf("{\"swd_test_flash\":{\"state\":\"ok\",\"addr\":\"0x%08lx\"}}\n",
               (unsigned long)scratch_addr);
}

// Re-run the SWD connectivity check against the running EP and update the
// cached state.  Used by the OTA EP test suite after reflashing EP to
// confirm the new image booted successfully.
static void cmd_swd_test_connect(void)
{
    SWDLock lock;

    if (!gpio_get(EP_SWD_DIS_PIN)) {
        s_swd_state = SWD_INHIBITED;
        vfy_printf("{\"swd_test_connect\":{\"state\":\"inhibited\"}}\n");
        return;
    }

    if (!swd->connect_target(0, false)) {
        s_swd_state = SWD_CONNECT_FAIL;
        vfy_printf("{\"swd_test_connect\":{\"state\":\"connect_fail\"}}\n");
        return;
    }

    static const uint32_t ADDR = 0x20040000u;  // SCRATCH_X — safe from Core 1 writes
    static const uint32_t PAT[4] = {
        0xDEADBEEFu, 0x12345678u, 0xCAFEBABEu, 0xA5A5A5A5u
    };
    uint32_t rb[4] = {};
    if (!swd->write_target_mem(ADDR, PAT, sizeof(PAT)) ||
        !swd->read_target_mem(ADDR, rb, sizeof(rb))    ||
        memcmp(PAT, rb, sizeof(PAT)) != 0) {
        s_swd_state = SWD_ROUNDTRIP_FAIL;
        vfy_printf("{\"swd_test_connect\":{\"state\":\"roundtrip_fail\"}}\n");
        return;
    }

    s_swd_state = SWD_READY;
    vfy_printf("{\"swd_test_connect\":{\"state\":\"ready\"}}\n");
}

// -------------------------------------------------------------------------
static void cmd_ota(void)
{
    int32_t boot_slot   = FlashWp::get_boot_slot();
    int32_t target_slot = FlashWp::get_target_slot();
    bool    available   = FlashWp::get_ota_availability();
    vfy_printf("{\"ota\":{\"state\":\"%s\",\"boot_slot\":%ld,\"target_slot\":%ld}}\n",
               available ? "available" : "unavailable",
               (long)boot_slot, (long)target_slot);
}

// -------------------------------------------------------------------------
static void cmd_unknown(const char* name)
{
    vfy_printf("{\"unknown\":{\"cmd\":\"%s\"}}\n", name);
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

                    char* sp = strchr(buf, ' ');
                    const char* arg = sp ? sp + 1 : nullptr;
                    if (sp) *sp = '\0';

                    if      (strcmp(buf, "boot")                    == 0) cmd_boot();
                    else if (strcmp(buf, "sd")                      == 0) cmd_sd();
                    else if (strcmp(buf, "filesystem")              == 0) cmd_filesystem();
                    else if (strcmp(buf, "gps")                     == 0) cmd_gps();
                    else if (strcmp(buf, "ecu")                     == 0) cmd_ecu();
                    else if (strcmp(buf, "swd")                     == 0) cmd_swd();
                    else if (strcmp(buf, "wifi")                    == 0) cmd_wifi();
                    else if (strcmp(buf, "heap")                    == 0) cmd_heap();
                    else if (strcmp(buf, "status")                  == 0) cmd_status();
                    else if (strcmp(buf, "ota")                     == 0) cmd_ota();
                    else if (strcmp(buf, "swd_test_connect")        == 0) cmd_swd_test_connect();
                    else if (strcmp(buf, "swd_test_flash")          == 0) cmd_swd_test_flash(arg);
                    else if (strcmp(buf, "filesystem_test_rw")      == 0) cmd_filesystem_test_rw();
                    else if (strcmp(buf, "filesystem_test_delete")  == 0) cmd_filesystem_test_delete(arg);
                    else if (strcmp(buf, "logger_stop")               == 0) cmd_logger_stop();
                    else if (strcmp(buf, "logger_start")             == 0) cmd_logger_start();
                    else if (strcmp(buf, "logstore_fsck")             == 0) cmd_logstore_fsck();
                    else if (strcmp(buf, "logstore_test_chunk_crossing") == 0) cmd_logstore_test_chunk_crossing();
                    else if (strcmp(buf, "logstore_write_test_meta") == 0) cmd_logstore_write_test_meta(arg);
                    else                                                   cmd_unknown(buf);

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
// Run the SWD boot-time connectivity check against the already-running EP.
// Call from main after swd is created and g_swd_mutex exists.
// No EP reset is performed — EP is already running normally at this point.
void swd_boot_check(void)
{
    if (!gpio_get(EP_SWD_DIS_PIN)) {
        s_swd_state = SWD_INHIBITED;
        return;
    }

    SWDLock lock;

    if (!swd->connect_target(0, false)) {
        s_swd_state = SWD_CONNECT_FAIL;
        return;
    }

    static const uint32_t ADDR = 0x20040000u;  // SCRATCH_X — safe from Core 1 writes
    static const uint32_t PAT[4] = {
        0xDEADBEEFu, 0x12345678u, 0xCAFEBABEu, 0xA5A5A5A5u
    };
    uint32_t rb[4] = {};
    if (!swd->write_target_mem(ADDR, PAT, sizeof(PAT)) ||
        !swd->read_target_mem(ADDR, rb, sizeof(rb))    ||
        memcmp(PAT, rb, sizeof(PAT)) != 0) {
        s_swd_state = SWD_ROUNDTRIP_FAIL;
        return;
    }

    s_swd_state = SWD_READY;
}

// -------------------------------------------------------------------------
void vfy_task_init(void)
{
    xTaskCreate(vfy_task, "vfy", 1024, nullptr, tskIDLE_PRIORITY + 1, nullptr);
}
