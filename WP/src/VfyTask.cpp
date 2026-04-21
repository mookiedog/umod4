#include "VfyTask.h"
#include "wp_rtt.h"
#include "lfsMgr.h"
#include "umod4_WP.h"

#include "FreeRTOS.h"
#include "task.h"

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

                    if      (strcmp(buf, "ping")     == 0) cmd_ping();
                    else if (strcmp(buf, "version")  == 0) cmd_version();
                    else if (strcmp(buf, "status")   == 0) cmd_status();
                    else if (strcmp(buf, "lfs_test") == 0) cmd_lfs_test();
                    else                                   cmd_unknown(buf);

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
