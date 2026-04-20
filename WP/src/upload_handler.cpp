/**
 * Upload handler for HTTP file uploads.
 *
 * File I/O operations are delegated to file_io_task to avoid calling
 * LittleFS functions directly from the lwIP HTTP callback context
 * (which has insufficient stack space for LFS operations).
 */

#include "upload_handler.h"
#include "file_io_task.h"
#include "api_handlers.h"
#include "FlashConfig.h"
#include "FlashEp.h"
#include "ota_flash_task.h"
#include "Swd.h"
#include "swd_lock.h"
#include "bsonlib.h"
#include "murmur3.h"
#include "pico/stdlib.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"  // For tcp_recved()
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern uint32_t get_last_crank_event_us(void);

// Verbose logging control (set to 1 for detailed upload progress)
#ifndef UPLOAD_VERBOSE
#define UPLOAD_VERBOSE 0
#endif

#if UPLOAD_VERBOSE
#define UPLOAD_LOG(...) printf(__VA_ARGS__)
#else
#define UPLOAD_LOG(...) do {} while(0)
#endif

// Always print errors and important events
#define UPLOAD_ERROR(...) printf(__VA_ARGS__)

// Timeout for file I/O operations (milliseconds)
#define FILE_IO_TIMEOUT_MS 5000

// Timeout for stale upload sessions (milliseconds)
// Sessions with no activity for this long can be evicted when slots are needed
// Reduced from 30s to 10s to release TCP connections faster and prevent pool exhaustion
#define UPLOAD_SESSION_TIMEOUT_MS 10000

// Global upload session table
static upload_session_t upload_sessions[MAX_UPLOAD_SESSIONS];

// Single-slot session for POST /api/config (body fits in one small buffer)
#define CONFIG_POST_BODY_MAX 768
typedef struct {
    void* connection;
    char  body[CONFIG_POST_BODY_MAX];
    size_t body_len;
    bool  active;
} config_post_session_t;
static config_post_session_t config_session;

// External flash config reference (owned by main.cpp)
extern flash_config_t g_flash_config;

// Single-slot session for POST /api/ecu-live-config
#define ECU_LIVE_CONFIG_POST_MAX 128
typedef struct {
    void* connection;
    char  body[ECU_LIVE_CONFIG_POST_MAX];
    size_t body_len;
    bool  active;
} ecu_live_config_post_session_t;
static ecu_live_config_post_session_t ecu_live_config_session;

// Single-slot session for POST /api/restore/<filename>
// No body buffer needed - bytes are streamed directly to LittleFS as they arrive.
typedef struct {
    void* connection;
    bool  active;
    bool  file_open;
    bool  error;
    char  filename[32];   // Target filename, e.g. "background.jpg"
} restore_post_session_t;
static restore_post_session_t restore_session;

// Forward declaration (defined later in this file)
static bool json_get_str(const char* json, const char* key, char* out, size_t out_size);

// ---------------------------------------------------------------------------
// Image-store sessions

// POST /api/image-store/upload: receive 32KB binary, stream to temp file, write slot.
// Body is exactly 32768 bytes of raw EPROM image data.
// Headers: X-IS-Name, X-IS-Description, X-IS-Protection (default "N")
#define IMGSTORE_BIN_SIZE 32768
#define IMGSTORE_TMP_PATH "/tmp_imgstore.bin"
typedef struct {
    void*  connection;
    bool   active;
    char   name[32];
    char   description[64];
    char   protection[4];
    uint32_t bytes_received;
    uint32_t target_slot_addr;  // 0 = find first empty slot; non-zero = overwrite this address
    bool   overflow;
    bool   file_open;  // true while /tmp_imgstore.bin is open via file_io upload
} imgstore_upload_session_t;
static imgstore_upload_session_t imgstore_upload_session;

// POST /api/image-store/delete: small JSON body, e.g. {"slot": N} or {"name": "UM4"}
#define IMGSTORE_DELETE_BODY_MAX 128
typedef struct {
    void*  connection;
    char   body[IMGSTORE_DELETE_BODY_MAX];
    size_t body_len;
    bool   active;
} imgstore_delete_session_t;
static imgstore_delete_session_t imgstore_delete_session;

// POST /api/image-store/selector: small JSON body like
// {"images":[{"code":"UM4"},{"code":"UM4","mapblob":"RP58"}]}
#define IMGSTORE_SELECTOR_BODY_MAX 512
typedef struct {
    void*  connection;
    char   body[IMGSTORE_SELECTOR_BODY_MAX];
    size_t body_len;
    bool   active;
} imgstore_selector_session_t;
static imgstore_selector_session_t imgstore_selector_session;

// POST /api/image-store/edit: update slot metadata, keep existing binary.
// No body. Headers: X-IS-Slot (1-127), X-IS-Name, X-IS-Description, X-IS-Protection.
typedef struct {
    void*  connection;
    bool   active;
    char   name[32];
    char   description[64];
    char   protection[4];
    int    slot_index;  // 1-127
} imgstore_edit_session_t;
static imgstore_edit_session_t imgstore_edit_session;

// ---------------------------------------------------------------------------
// Minimal BSON builder — write-only, for constructing slot headers and selector docs.

typedef struct { uint8_t* buf; size_t cap; size_t pos; bool overflow; } BsonWriter;

static void bw_byte(BsonWriter* w, uint8_t b)
{
    if (w->pos < w->cap) w->buf[w->pos] = b; else w->overflow = true;
    w->pos++;
}
static void bw_bytes(BsonWriter* w, const uint8_t* d, size_t n)
    { for (size_t i = 0; i < n; i++) bw_byte(w, d[i]); }
static void bw_cstr(BsonWriter* w, const char* s)
    { while (*s) bw_byte(w, (uint8_t)*s++); bw_byte(w, 0); }
static void bw_i32(BsonWriter* w, int32_t v) {
    bw_byte(w, (uint8_t)(v        & 0xFF));
    bw_byte(w, (uint8_t)((v >> 8) & 0xFF));
    bw_byte(w, (uint8_t)((v >>16) & 0xFF));
    bw_byte(w, (uint8_t)((v >>24) & 0xFF));
}
static void bw_init(BsonWriter* w, uint8_t* buf, size_t cap)
    { w->buf = buf; w->cap = cap; w->pos = 4; w->overflow = false; } // reserve 4 bytes for doc len

static void bson_write_utf8(BsonWriter* w, const char* key, const char* val) {
    bw_byte(w, 0x02);
    bw_cstr(w, key);
    bw_i32(w, (int32_t)strlen(val) + 1);
    bw_cstr(w, val);
}
static void bson_write_int32(BsonWriter* w, const char* key, int32_t val) {
    bw_byte(w, 0x10);
    bw_cstr(w, key);
    bw_i32(w, val);
}
static size_t bson_begin_doc(BsonWriter* w, uint8_t type, const char* key) {
    bw_byte(w, type); bw_cstr(w, key);
    size_t lp = w->pos; bw_i32(w, 0); return lp;
}
static void bson_end_doc(BsonWriter* w, size_t lp) {
    bw_byte(w, 0x00);
    int32_t n = (int32_t)(w->pos - lp);
    if (!w->overflow && lp + 4 <= w->cap) {
        w->buf[lp]   = (uint8_t)(n        & 0xFF);
        w->buf[lp+1] = (uint8_t)((n >> 8) & 0xFF);
        w->buf[lp+2] = (uint8_t)((n >>16) & 0xFF);
        w->buf[lp+3] = (uint8_t)((n >>24) & 0xFF);
    }
}
static size_t bson_finalise(BsonWriter* w) {
    bw_byte(w, 0x00);
    int32_t t = (int32_t)w->pos;
    if (!w->overflow && w->cap >= 4) {
        w->buf[0] = (uint8_t)(t        & 0xFF);
        w->buf[1] = (uint8_t)((t >> 8) & 0xFF);
        w->buf[2] = (uint8_t)((t >>16) & 0xFF);
        w->buf[3] = (uint8_t)((t >>24) & 0xFF);
    }
    return w->overflow ? 0 : (size_t)t;
}

// Build BSON header for an image slot: {name, description, image_m3, [protection if != "N"]}
// Returns byte count, 0 on overflow.
static size_t build_slot_bson_hdr(uint8_t* buf, size_t cap,
                                   const char* name, const char* description,
                                   uint32_t image_m3, const char* protection)
{
    BsonWriter w;
    bw_init(&w, buf, cap);
    bson_write_utf8(&w, "name", name);
    bson_write_utf8(&w, "description", description);
    bson_write_int32(&w, "image_m3", (int32_t)image_m3);
    if (protection && protection[0] != '\0' && strcmp(protection, "N") != 0)
        bson_write_utf8(&w, "protection", protection);
    return bson_finalise(&w);
}

// ---------------------------------------------------------------------------
// Engine-running guard: returns true if the engine has been running within 1 second.

static bool imgstore_engine_running(void)
{
    uint32_t last_crank = get_last_crank_event_us();
    return (last_crank != 0) && ((time_us_32() - last_crank) < 1000000u);
}

// Scan EP flash slots 1–127 via SWD to find the first empty slot (first 4 bytes == 0xFFFFFFFF).
// Returns the flash address of the empty slot, or 0 if all slots full or SWD unavailable.
static uint32_t find_empty_slot(void)
{
    SWDLock lock;
    const uint32_t BASE = 0x10200000;
    const uint32_t SLOT = 65536;
    if (!swd || !swd->connect_target(0, false)) return 0;
    for (int i = 1; i < 128; i++) {
        uint32_t first_word = 0;
        if (!swd->read_target_mem(BASE + (uint32_t)i * SLOT, &first_word, 4)) continue;
        if (first_word == 0xFFFFFFFF) return BASE + (uint32_t)i * SLOT;
    }
    return 0;
}

// Resolve a slot address from JSON body: looks for "slot":N or "name":"..."
// Returns the flash address, or 0 if not found / invalid.
static uint32_t resolve_slot_addr_from_json(const char* body)
{
    SWDLock lock;
    const uint32_t BASE = 0x10200000;
    const uint32_t SLOT = 65536;

    // Try "slot": N
    char val[32];
    if (json_get_str(body, "slot", val, sizeof(val))) {
        int n = atoi(val);
        if (n >= 1 && n <= 127) return BASE + (uint32_t)n * SLOT;
    }
    // Also try integer form: "slot":N without quotes
    const char* sp = strstr(body, "\"slot\":");
    if (sp) {
        sp += 7; while (*sp == ' ') sp++;
        if (*sp >= '0' && *sp <= '9') {
            int n = atoi(sp);
            if (n >= 1 && n <= 127) return BASE + (uint32_t)n * SLOT;
        }
    }

    // Try "name": "..." — scan slots to find it
    if (!json_get_str(body, "name", val, sizeof(val)) || val[0] == '\0') return 0;
    if (!swd || !swd->connect_target(0, false)) return 0;

    static uint8_t hdr[256 + 4];
    for (int i = 1; i < 128; i++) {
        uint32_t slot_addr = BASE + (uint32_t)i * SLOT;
        uint32_t first_word = 0;
        if (!swd->read_target_mem(slot_addr, &first_word, 4)) continue;
        if (first_word == 0xFFFFFFFF) continue;
        uint32_t dsz = Bson::read_unaligned_uint32((const uint8_t*)&first_word);
        if (dsz < 5 || dsz > 32768) continue;
        uint32_t read_bytes = (dsz < 256) ? ((dsz + 3) & ~3u) : 256;
        if (!swd->read_target_mem(slot_addr, (uint32_t*)hdr, read_bytes)) continue;
        hdr[255] = 0;
        element_t e;
        if (Bson::findElement(hdr, "name", e) && e.elementType == BSON_TYPE_UTF8) {
            const char* slot_name = (const char*)e.data + 4;
            if (strcmp(slot_name, val) == 0) return slot_addr;
        }
    }
    return 0;
}

// Minimal JSON string field extractor: finds "key":"value" and copies value into out.
// Returns true on success.
static bool json_get_str(const char* json, const char* key,
                         char* out, size_t out_size)
{
    // Search for "key":
    char search[80];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char* p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return true;
}

// Minimal JSON uint16 extractor: finds "key":number
static bool json_get_u16(const char* json, const char* key, uint16_t* out)
{
    char search[80];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char* p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p == ' ') p++;
    if (*p < '0' || *p > '9') return false;
    *out = (uint16_t)atoi(p);
    return true;
}

// Forward declarations for helper functions
static upload_session_t* find_session_by_connection(void* connection);
static upload_session_t* find_session_by_id(const char* session_id);
static upload_session_t* allocate_session(void* connection);
static void free_session(upload_session_t* session);
static const char* extract_header_value(const char* headers, const char* header_name,
                                        char* value_buffer, size_t buffer_size);
static bool validate_filename(const char* filename);

void upload_handler_init(void)
{
    memset(upload_sessions, 0, sizeof(upload_sessions));
    UPLOAD_LOG("upload_handler: Initialized (%d max sessions)\n", MAX_UPLOAD_SESSIONS);
}

err_t upload_post_begin(void *connection, const char *uri,
                        const char *http_request, u16_t http_request_len,
                        int content_len, char *response_uri,
                        u16_t response_uri_len, u8_t *post_auto_wnd)
{
    UPLOAD_LOG("u_p_b: URI=%s, content_len=%d\n", uri, content_len);

    // Handle POST /api/config
    if (strcmp(uri, "/api/config") == 0) {
        if (content_len > CONFIG_POST_BODY_MAX - 1) {
            UPLOAD_ERROR("u_p_b: /api/config body too large (%d)\n", content_len);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }
        memset(&config_session, 0, sizeof(config_session));
        config_session.connection = connection;
        config_session.active = true;
        snprintf(response_uri, response_uri_len, "/upload_success.json");
        return ERR_OK;
    }

    // Handle POST /api/ecu-live-config
    if (strcmp(uri, "/api/ecu-live-config") == 0) {
        if (content_len > ECU_LIVE_CONFIG_POST_MAX - 1) {
            UPLOAD_ERROR("u_p_b: /api/ecu-live-config body too large (%d)\n", content_len);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }
        memset(&ecu_live_config_session, 0, sizeof(ecu_live_config_session));
        ecu_live_config_session.connection = connection;
        ecu_live_config_session.active = true;
        snprintf(response_uri, response_uri_len, "/upload_success.json");
        return ERR_OK;
    }

    // Handle POST /api/restore/<filename>
    if (strncmp(uri, "/api/restore/", 13) == 0) {
        const char* filename = uri + 13;
        bool allowed = (strcmp(filename, "background.jpg") == 0 ||
                        strcmp(filename, "ecu_live.json") == 0);
        if (!allowed) {
            UPLOAD_ERROR("u_p_b: /api/restore: rejected '%s'\n", filename);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }
        memset(&restore_session, 0, sizeof(restore_session));
        restore_session.connection = connection;
        strncpy(restore_session.filename, filename, sizeof(restore_session.filename) - 1);

        char filepath[64];
        snprintf(filepath, sizeof(filepath), "/%s", filename);
        file_io_result_t io_result;
        if (!file_io_upload_open(filepath, true, FILE_IO_TIMEOUT_MS, &io_result)
                || !io_result.success) {
            UPLOAD_ERROR("u_p_b: /api/restore: open '%s' failed\n", filepath);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }
        restore_session.file_open = true;
        restore_session.active    = true;
        snprintf(response_uri, response_uri_len, "/upload_success.json");
        return ERR_OK;
    }

    // Handle POST /api/image-store/upload
    if (strcmp(uri, "/api/image-store/upload") == 0) {
        if (imgstore_engine_running()) {
            UPLOAD_ERROR("u_p_b: /api/image-store/upload rejected: engine running\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }
        if (content_len != IMGSTORE_BIN_SIZE) {
            UPLOAD_ERROR("u_p_b: /api/image-store/upload: bad content_len %d (expected %d)\n",
                         content_len, IMGSTORE_BIN_SIZE);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }
        memset(&imgstore_upload_session, 0, sizeof(imgstore_upload_session));
        imgstore_upload_session.connection = connection;
        extract_header_value(http_request, "X-IS-Name:", imgstore_upload_session.name,
                             sizeof(imgstore_upload_session.name));
        extract_header_value(http_request, "X-IS-Description:", imgstore_upload_session.description,
                             sizeof(imgstore_upload_session.description));
        if (!extract_header_value(http_request, "X-IS-Protection:", imgstore_upload_session.protection,
                                  sizeof(imgstore_upload_session.protection))) {
            strncpy(imgstore_upload_session.protection, "N", sizeof(imgstore_upload_session.protection));
        }
        {
            char slot_str[8] = {0};
            if (extract_header_value(http_request, "X-IS-Slot:", slot_str, sizeof(slot_str))) {
                int si = atoi(slot_str);
                if (si >= 1 && si <= 127) {
                    imgstore_upload_session.target_slot_addr =
                        0x10200000u + (uint32_t)si * 65536u;
                }
            }
        }
        if (imgstore_upload_session.name[0] == '\0') {
            UPLOAD_ERROR("u_p_b: /api/image-store/upload: missing X-IS-Name\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }
        {
            file_io_result_t io_result;
            if (!file_io_upload_open(IMGSTORE_TMP_PATH, true,
                                     FILE_IO_TIMEOUT_MS, &io_result)
                    || !io_result.success) {
                UPLOAD_ERROR("u_p_b: /api/image-store/upload: open tmp file failed\n");
                snprintf(response_uri, response_uri_len, "/upload_error.json");
                return ERR_OK;
            }
        }
        imgstore_upload_session.file_open = true;
        imgstore_upload_session.active = true;
        snprintf(response_uri, response_uri_len, "/upload_success.json");
        return ERR_OK;
    }

    // Handle POST /api/image-store/delete
    if (strcmp(uri, "/api/image-store/delete") == 0) {
        if (imgstore_engine_running()) {
            UPLOAD_ERROR("u_p_b: /api/image-store/delete rejected: engine running\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }
        if (content_len > IMGSTORE_DELETE_BODY_MAX - 1) {
            UPLOAD_ERROR("u_p_b: /api/image-store/delete body too large (%d)\n", content_len);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }
        memset(&imgstore_delete_session, 0, sizeof(imgstore_delete_session));
        imgstore_delete_session.connection = connection;
        imgstore_delete_session.active = true;
        snprintf(response_uri, response_uri_len, "/upload_success.json");
        return ERR_OK;
    }

    // Handle POST /api/image-store/selector
    if (strcmp(uri, "/api/image-store/selector") == 0) {
        if (imgstore_engine_running()) {
            UPLOAD_ERROR("u_p_b: /api/image-store/selector rejected: engine running\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }
        if (content_len > IMGSTORE_SELECTOR_BODY_MAX - 1) {
            UPLOAD_ERROR("u_p_b: /api/image-store/selector body too large (%d)\n", content_len);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }
        memset(&imgstore_selector_session, 0, sizeof(imgstore_selector_session));
        imgstore_selector_session.connection = connection;
        imgstore_selector_session.active = true;
        snprintf(response_uri, response_uri_len, "/upload_success.json");
        return ERR_OK;
    }

    // Handle POST /api/image-store/edit
    if (strcmp(uri, "/api/image-store/edit") == 0) {
        if (imgstore_engine_running()) {
            UPLOAD_ERROR("u_p_b: /api/image-store/edit rejected: engine running\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }
        char slot_str[8] = {0};
        extract_header_value(http_request, "X-IS-Slot:", slot_str, sizeof(slot_str));
        int slot_index = atoi(slot_str);
        if (slot_index < 1 || slot_index > 127) {
            UPLOAD_ERROR("u_p_b: /api/image-store/edit: invalid slot %d\n", slot_index);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }
        memset(&imgstore_edit_session, 0, sizeof(imgstore_edit_session));
        imgstore_edit_session.connection = connection;
        imgstore_edit_session.slot_index = slot_index;
        extract_header_value(http_request, "X-IS-Name:", imgstore_edit_session.name,
                             sizeof(imgstore_edit_session.name));
        extract_header_value(http_request, "X-IS-Description:", imgstore_edit_session.description,
                             sizeof(imgstore_edit_session.description));
        if (!extract_header_value(http_request, "X-IS-Protection:", imgstore_edit_session.protection,
                                  sizeof(imgstore_edit_session.protection))) {
            strncpy(imgstore_edit_session.protection, "N", sizeof(imgstore_edit_session.protection));
        }
        if (imgstore_edit_session.name[0] == '\0') {
            UPLOAD_ERROR("u_p_b: /api/image-store/edit: missing X-IS-Name\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }
        imgstore_edit_session.active = true;
        printf("u_p_b: /api/image-store/edit slot=%d name='%s'\n",
               slot_index, imgstore_edit_session.name);
        snprintf(response_uri, response_uri_len, "/upload_success.json");
        return ERR_OK;
    }

    // Only handle /api/upload endpoint
    if (strncmp(uri, "/api/upload", 11) != 0) {
        return ERR_VAL;  // Not our endpoint
    }

    // Parse HTTP headers to extract upload metadata
    // Headers we expect:
    // X-Session-ID: <uuid> (optional for new uploads)
    // X-Filename: <filename>
    // X-Total-Size: <bytes>
    // X-Chunk-Size: <bytes>
    // X-Chunk-Offset: <offset>
    // X-Chunk-CRC32: <hex> (optional for verification)

    // Thread-safe: Use stack buffer for header parsing
    char header_buffer[256];
    char session_id[64] = {0};
    char filename[64] = {0};
    uint32_t total_size = 0;
    uint32_t chunk_size = 0;
    uint32_t chunk_offset = 0;

    const char* tmp;

    tmp = extract_header_value(http_request, "X-Session-ID:", header_buffer, sizeof(header_buffer));
    if (tmp) {
        strncpy(session_id, tmp, sizeof(session_id) - 1);
    }

    tmp = extract_header_value(http_request, "X-Filename:", header_buffer, sizeof(header_buffer));
    if (tmp) {
        strncpy(filename, tmp, sizeof(filename) - 1);
    }

    tmp = extract_header_value(http_request, "X-Total-Size:", header_buffer, sizeof(header_buffer));
    if (tmp) {
        total_size = (uint32_t)atoi(tmp);
    }

    tmp = extract_header_value(http_request, "X-Chunk-Size:", header_buffer, sizeof(header_buffer));
    if (tmp) {
        chunk_size = (uint32_t)atoi(tmp);
    }

    tmp = extract_header_value(http_request, "X-Chunk-Offset:", header_buffer, sizeof(header_buffer));
    if (tmp) {
        chunk_offset = (uint32_t)atoi(tmp);
    }

    // Validate required headers
    if (filename[0] == '\0' || total_size == 0 || chunk_size == 0) {
        UPLOAD_ERROR("u_p_b: Missing required headers (filename='%s', total=%lu, chunk=%lu)\n",
               filename, (unsigned long)total_size, (unsigned long)chunk_size);
        snprintf(response_uri, response_uri_len, "/upload_error.json");
        return ERR_OK;
    }

    // Validate filename (security: prevent path traversal)
    if (!validate_filename(filename)) {
        UPLOAD_ERROR("u_p_b: Invalid filename '%s'\n", filename);
        snprintf(response_uri, response_uri_len, "/upload_error.json");
        return ERR_OK;
    }

    printf("%s: '%s', tot=%lu, chnk=%lu, off=%lu\r",
           __FUNCTION__, filename, (unsigned long)total_size, (unsigned long)chunk_size, (unsigned long)chunk_offset);

    // Find or create session
    upload_session_t* session = NULL;

    if (session_id[0] != '\0') {
        // Resuming existing session
        session = find_session_by_id(session_id);
        if (session) {
            char* sp = strrchr(session_id, '-');
            UPLOAD_LOG("u_p_b: Resume session ...%s at off %lu (chunk %u)\n",
                   sp/*session_id*/, (unsigned long)chunk_offset, (unsigned long)chunk_offset/32768);

            // Verify parameters match
            if (strcmp(session->filename, filename) != 0 ||
                session->total_size != total_size) {
                UPLOAD_ERROR("u_p_b: Session parameters mismatch\n");
                free_session(session);
                snprintf(response_uri, response_uri_len, "/upload_error.json");
                return ERR_OK;
            }

            // Verify offset matches current position
            if (chunk_offset != session->bytes_received) {
                UPLOAD_ERROR("u_p_b: Offset mismatch (expected=%lu, got=%lu)\n",
                       (unsigned long)session->bytes_received, (unsigned long)chunk_offset);
                // Free session to prevent upload_post_receive_data and
                // upload_post_finished from finding it via connection pointer
                // (HTTP keep-alive reuses the same connection for all chunks)
                free_session(session);
                snprintf(response_uri, response_uri_len, "/upload_error.json");
                return ERR_OK;
            }
        } else {
            UPLOAD_LOG("u_p_b: Session %s not found, creating new\n", session_id);
        }
    }

    if (!session) {
        // Create new session
        session = allocate_session(connection);
        if (!session) {
            UPLOAD_ERROR("u_p_b: Failed to allocate session (all slots busy)\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }

        // Initialize session
        if (session_id[0] != '\0') {
            strncpy(session->session_id, session_id, sizeof(session->session_id) - 1);
        } else {
            // Generate UUID (simplified - use connection pointer + timestamp + file size)
            uint32_t t = time_us_32();
            snprintf(session->session_id, sizeof(session->session_id),
                     "%08lx-%04lx-%04lx-%04x-%012lx",
                     (unsigned long)t,
                     (unsigned long)((uintptr_t)connection >> 16) & 0xFFFF,
                     (unsigned long)((uintptr_t)connection) & 0xFFFF,
                     (uint16_t)(total_size & 0xFFFF),
                     (unsigned long)((uint64_t)t * 1000000ULL + total_size) & 0xFFFFFFFFFFFFULL);
        }
        session->session_id[sizeof(session->session_id) - 1] = '\0';

        strncpy(session->filename, filename, sizeof(session->filename) - 1);
        session->filename[sizeof(session->filename) - 1] = '\0';
        session->total_size = total_size;
        session->chunk_size = chunk_size;
        session->bytes_received = 0;
        session->file_open = false;

        // Initialize ping-pong buffers (already allocated in allocate_session)
        session->active_buffer = 0;  // Start with buffer A
        session->buffer_used[0] = 0;
        session->buffer_used[1] = 0;
        session->write_in_progress = false;

        // Build full file path (store in root directory)
        file_io_result_t result;
        char filepath[80];
        snprintf(filepath, sizeof(filepath), "/%s", session->filename);

        // Open file for writing (via file_io_task)
        bool truncate = (chunk_offset == 0);  // New upload - truncate; Resume - append
        if (!file_io_upload_open(filepath, truncate, FILE_IO_TIMEOUT_MS, &result)) {
            UPLOAD_ERROR("u_p_b: open timeout or error\n");
            free_session(session);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }

        if (!result.success) {
            UPLOAD_ERROR("u_p_b: Failed to open '%s': %s\n", filepath, result.error_message);
            free_session(session);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return ERR_OK;
        }

        session->file_open = true;

        // Initialize SHA-256
        if (pico_sha256_try_start(&session->sha_state, SHA256_BIG_ENDIAN, true) == PICO_OK) {
            session->sha_enabled = true;
            UPLOAD_LOG("u_p_b: SHA-256 enabled\n");
        } else {
            session->sha_enabled = false;
            UPLOAD_ERROR("u_p_b: WARNING: SHA-256 hardware busy\n");
        }

        UPLOAD_LOG("u_p_b: Created session %s for '%s' (%lu bytes)\n",
               session->session_id, filename, (unsigned long)total_size);
    }

    // Track connection lifecycle (only in verbose mode - lwIP httpd closes POST connections)
    #if UPLOAD_VERBOSE
    static void* last_connection = NULL;
    if (connection != last_connection) {
        printf("u_p_b: NEW conn %p (prev %p)\n", connection, last_connection);
        last_connection = connection;
    } else {
        printf("u_p_b: REUSING conn %p\n", connection);
    }
    #endif

    // Log Connection header from client request (verbose mode only)
    #if UPLOAD_VERBOSE
    const char* conn_hdr = extract_header_value(http_request, "Connection:", header_buffer, sizeof(header_buffer));
    if (conn_hdr) {
        printf("u_p_b: Client sent 'Connection: %s'\n", conn_hdr);
    } else {
        printf("u_p_b: Client sent NO Connection header\n");
    }
    #endif

    // Handle session re-entry race: If session was already tied to a different connection,
    // just log it and update to the new connection. We cannot call tcp_abort() on the old
    // connection because lwIP httpd manages PCBs internally and calling lwIP functions on
    // them directly causes crashes. The old connection will timeout naturally via httpd's
    // own polling mechanism.
    if (session->connection && session->connection != connection) {
        UPLOAD_LOG("u_p_b: Session %s resumed on new conn %p (old conn %p will timeout)\n",
                   session->session_id, connection, session->connection);
    }

    // Update connection pointer and activity timestamp (might have changed on resume)
    session->connection = connection;
    session->last_activity_ms = to_ms_since_boot(get_absolute_time());

    // Enable automatic window updates for faster transfers
    *post_auto_wnd = 1;

    // Response will be generated in upload_post_finished
    return ERR_OK;
}

err_t upload_post_receive_data(void *connection, struct pbuf *p)
{
    // Check if this is a /api/restore POST
    if (restore_session.active && restore_session.connection == connection) {
        if (!restore_session.error && restore_session.file_open) {
            struct pbuf* q = p;
            while (q) {
                file_io_result_t io_result;
                if (!file_io_upload_write((const uint8_t*)q->payload, q->len,
                                           FILE_IO_TIMEOUT_MS, &io_result)
                        || !io_result.success) {
                    UPLOAD_ERROR("u_p_r: /api/restore write failed: %s\n",
                                 io_result.error_message);
                    restore_session.error = true;
                    file_io_upload_close(false, FILE_IO_TIMEOUT_MS, &io_result);
                    restore_session.file_open = false;
                    break;
                }
                q = q->next;
            }
        }
        tcp_recved((struct tcp_pcb*)connection, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }

    // Check if this is a /api/config POST
    if (config_session.active && config_session.connection == connection) {
        struct pbuf* q = p;
        while (q) {
            size_t space = CONFIG_POST_BODY_MAX - 1 - config_session.body_len;
            size_t copy = (q->len < space) ? q->len : space;
            memcpy(config_session.body + config_session.body_len, q->payload, copy);
            config_session.body_len += copy;
            q = q->next;
        }
        config_session.body[config_session.body_len] = '\0';
        tcp_recved((struct tcp_pcb*)connection, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }

    // Check if this is a /api/ecu-live-config POST
    if (ecu_live_config_session.active && ecu_live_config_session.connection == connection) {
        struct pbuf* q = p;
        while (q) {
            size_t space = ECU_LIVE_CONFIG_POST_MAX - 1 - ecu_live_config_session.body_len;
            size_t copy = (q->len < space) ? q->len : space;
            memcpy(ecu_live_config_session.body + ecu_live_config_session.body_len,
                   q->payload, copy);
            ecu_live_config_session.body_len += copy;
            q = q->next;
        }
        ecu_live_config_session.body[ecu_live_config_session.body_len] = '\0';
        tcp_recved((struct tcp_pcb*)connection, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }

    // Check if this is a /api/image-store/upload POST — stream to temp file
    if (imgstore_upload_session.active && imgstore_upload_session.connection == connection) {
        struct pbuf* q = p;
        while (q) {
            if (imgstore_upload_session.file_open && !imgstore_upload_session.overflow) {
                file_io_result_t io_result;
                if (!file_io_upload_write((const uint8_t*)q->payload, q->len,
                                          FILE_IO_TIMEOUT_MS, &io_result)
                        || !io_result.success) {
                    imgstore_upload_session.overflow = true;
                    file_io_result_t tmp;
                    file_io_upload_close(false, FILE_IO_TIMEOUT_MS, &tmp);
                    imgstore_upload_session.file_open = false;
                } else {
                    imgstore_upload_session.bytes_received += q->len;
                }
            }
            q = q->next;
        }
        tcp_recved((struct tcp_pcb*)connection, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }

    // Check if this is a /api/image-store/delete POST
    if (imgstore_delete_session.active && imgstore_delete_session.connection == connection) {
        struct pbuf* q = p;
        while (q) {
            size_t space = IMGSTORE_DELETE_BODY_MAX - 1 - imgstore_delete_session.body_len;
            size_t copy = (q->len < space) ? q->len : space;
            memcpy(imgstore_delete_session.body + imgstore_delete_session.body_len, q->payload, copy);
            imgstore_delete_session.body_len += copy;
            q = q->next;
        }
        imgstore_delete_session.body[imgstore_delete_session.body_len] = '\0';
        tcp_recved((struct tcp_pcb*)connection, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }

    // Check if this is a /api/image-store/selector POST
    if (imgstore_selector_session.active && imgstore_selector_session.connection == connection) {
        struct pbuf* q = p;
        while (q) {
            size_t space = IMGSTORE_SELECTOR_BODY_MAX - 1 - imgstore_selector_session.body_len;
            size_t copy = (q->len < space) ? q->len : space;
            memcpy(imgstore_selector_session.body + imgstore_selector_session.body_len, q->payload, copy);
            imgstore_selector_session.body_len += copy;
            q = q->next;
        }
        imgstore_selector_session.body[imgstore_selector_session.body_len] = '\0';
        tcp_recved((struct tcp_pcb*)connection, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }

    // Check if this is a /api/image-store/edit POST (no body expected, nothing to accumulate)
    if (imgstore_edit_session.active && imgstore_edit_session.connection == connection) {
        tcp_recved((struct tcp_pcb*)connection, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }

    upload_session_t* session = find_session_by_connection(connection);
    if (!session) {
        UPLOAD_ERROR("upload_post_receive_data: No session found for connection\n");
        pbuf_free(p);
        return ERR_VAL;
    }

    // Buffers are now statically allocated - always valid, no need to check

    // Ping-pong buffer strategy: Receive into one buffer while writing the other.
    // When active buffer fills up:
    // 1. Wait for previous async write to complete (if one is pending)
    // 2. Submit async write of full buffer
    // 3. Swap to the other buffer
    // This decouples network receive from SD writes, preventing TCP window stalls.

    struct pbuf* q = p;
    while (q != NULL) {
        uint8_t* active_buf = (session->active_buffer == 0) ?
                              session->write_buffer_a : session->write_buffer_b;
        uint32_t* active_used = &session->buffer_used[session->active_buffer];

        // Check if adding this pbuf would overflow active buffer
        if (*active_used + q->len > UPLOAD_BUFFER_SIZE) {
            // Active buffer is full or would overflow - need to flush it

            if (*active_used > 0 && session->file_open) {
                // Wait for previous async write to complete before starting new one
                if (session->write_in_progress) {
                    uint32_t wait_start = to_ms_since_boot(get_absolute_time());
                    UPLOAD_LOG("[UPLOAD] Waiting for async write completion at offset %lu...\n",
                               (unsigned long)session->bytes_received);

                    file_io_result_t result;
                    if (!file_io_upload_write_wait(FILE_IO_TIMEOUT_MS, &result)) {
                        uint32_t wait_duration = to_ms_since_boot(get_absolute_time()) - wait_start;
                        UPLOAD_ERROR("[UPLOAD] TIMEOUT waiting for async write after %lu ms at offset %lu\n",
                               (unsigned long)wait_duration, (unsigned long)session->bytes_received);
                        free_session(session);
                        pbuf_free(p);
                        return ERR_VAL;
                    }
                    if (!result.success) {
                        UPLOAD_ERROR("[UPLOAD] Write FAILED: %s at offset %lu\n",
                               result.error_message, (unsigned long)session->bytes_received);
                        free_session(session);
                        pbuf_free(p);
                        return ERR_VAL;
                    }

                    // Log wait duration (always log in verbose mode)
                    uint32_t wait_duration = to_ms_since_boot(get_absolute_time()) - wait_start;
                    if (wait_duration > 100 || UPLOAD_VERBOSE) {
                        UPLOAD_LOG("[UPLOAD] Async write wait: %lu ms for %lu bytes at offset %lu\n",
                               (unsigned long)wait_duration,
                               (unsigned long)result.write_result.bytes_written,
                               (unsigned long)session->bytes_received);
                    }
                    session->write_in_progress = false;
                }

                // Submit async write of full buffer (returns immediately)
                UPLOAD_LOG("[UPLOAD] Submitting async write of %lu bytes at offset %lu\n",
                           (unsigned long)*active_used, (unsigned long)session->bytes_received);

                if (!file_io_upload_write_async(active_buf, *active_used, FILE_IO_TIMEOUT_MS)) {
                    UPLOAD_ERROR("[UPLOAD] FAILED to submit async write at offset %lu\n",
                           (unsigned long)session->bytes_received);
                    free_session(session);
                    pbuf_free(p);
                    return ERR_VAL;
                }

                session->write_in_progress = true;

                // Swap to other buffer
                session->active_buffer = 1 - session->active_buffer;
                active_buf = (session->active_buffer == 0) ?
                             session->write_buffer_a : session->write_buffer_b;
                active_used = &session->buffer_used[session->active_buffer];
                *active_used = 0;
            }
        }

        // Copy pbuf data to active buffer
        memcpy(active_buf + *active_used, q->payload, q->len);
        *active_used += q->len;

        // Update SHA-256
        if (session->sha_enabled) {
            pico_sha256_update_blocking(&session->sha_state, (const uint8_t*)q->payload, q->len);
        }

        session->bytes_received += q->len;
        session->last_activity_ms = to_ms_since_boot(get_absolute_time());
        q = q->next;
    }

    // CRITICAL: Tell lwIP we've consumed the data so it can advertise a larger TCP window.
    // Without this, the window shrinks to zero and connections close/timeout.
    tcp_recved((struct tcp_pcb*)connection, p->tot_len);

    pbuf_free(p);
    return ERR_OK;
}

// External device name global (owned by main.cpp)
extern char g_device_name[64];

void upload_post_finished(void *connection, char *response_uri, u16_t response_uri_len)
{
    UPLOAD_LOG("u_p_f: Called for conn=%p\n", connection);

    // Handle /api/restore completion
    if (restore_session.active && restore_session.connection == connection) {
        restore_session.active = false;
        if (restore_session.error || !restore_session.file_open) {
            printf("u_p_f: /api/restore/%s: failed\n", restore_session.filename);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }
        file_io_result_t io_result;
        if (!file_io_upload_close(true, FILE_IO_TIMEOUT_MS, &io_result)
                || !io_result.success) {
            printf("u_p_f: /api/restore/%s: close failed\n", restore_session.filename);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }
        restore_session.file_open = false;
        printf("u_p_f: /api/restore/%s: saved OK\n", restore_session.filename);
        snprintf(response_uri, response_uri_len, "/upload_success.json");
        return;
    }

    // Handle /api/config POST
    if (config_session.active && config_session.connection == connection) {
        config_session.active = false;
        printf("u_p_f: /api/config body (%zu bytes): %s\n",
               config_session.body_len, config_session.body);

        const char* body = config_session.body;
        bool changed = false;

        // Apply each field if present in JSON (skip empty password fields)
        char tmp_str[64];
        uint16_t tmp_u16;

        if (json_get_str(body, "device_name", tmp_str, sizeof(tmp_str)) && tmp_str[0]) {
            strncpy(g_flash_config.device_name, tmp_str, sizeof(g_flash_config.device_name) - 1);
            g_flash_config.device_name[sizeof(g_flash_config.device_name) - 1] = '\0';
            strncpy(g_device_name, g_flash_config.device_name, sizeof(g_device_name) - 1);
            g_device_name[sizeof(g_device_name) - 1] = '\0';
            changed = true;
        }
        if (json_get_str(body, "wifi_ssid", tmp_str, sizeof(tmp_str)) && tmp_str[0]) {
            strncpy(g_flash_config.wifi_ssid, tmp_str, sizeof(g_flash_config.wifi_ssid) - 1);
            g_flash_config.wifi_ssid[sizeof(g_flash_config.wifi_ssid) - 1] = '\0';
            changed = true;
        }
        // Only update password if explicitly provided (non-empty)
        if (json_get_str(body, "wifi_password", tmp_str, sizeof(tmp_str)) && tmp_str[0]) {
            strncpy(g_flash_config.wifi_password, tmp_str, sizeof(g_flash_config.wifi_password) - 1);
            g_flash_config.wifi_password[sizeof(g_flash_config.wifi_password) - 1] = '\0';
            changed = true;
        }
        if (json_get_str(body, "server_host", tmp_str, sizeof(tmp_str)) && tmp_str[0]) {
            strncpy(g_flash_config.server_host, tmp_str, sizeof(g_flash_config.server_host) - 1);
            g_flash_config.server_host[sizeof(g_flash_config.server_host) - 1] = '\0';
            changed = true;
        }
        if (json_get_u16(body, "server_port", &tmp_u16) && tmp_u16 > 0) {
            g_flash_config.server_port = tmp_u16;
            changed = true;
        }
        if (json_get_str(body, "ap_ssid", tmp_str, sizeof(tmp_str))) {
            // Allow clearing ap_ssid (empty = use auto-generated name)
            strncpy(g_flash_config.ap_ssid, tmp_str, sizeof(g_flash_config.ap_ssid) - 1);
            g_flash_config.ap_ssid[sizeof(g_flash_config.ap_ssid) - 1] = '\0';
            changed = true;
        }
        if (json_get_str(body, "ap_password", tmp_str, sizeof(tmp_str)) && tmp_str[0]) {
            strncpy(g_flash_config.ap_password, tmp_str, sizeof(g_flash_config.ap_password) - 1);
            g_flash_config.ap_password[sizeof(g_flash_config.ap_password) - 1] = '\0';
            changed = true;
        }

        if (changed) {
            // Do NOT call flash_config_save() here — we're on the lwIP tcpip thread.
            // Disabling interrupts for flash erase/program corrupts the CYW43 driver
            // state. Instead, queue the save+reboot to the OTA task, which performs
            // the save AFTER shutdown_wifi() when the CYW43 is already powered off.
            printf("u_p_f: /api/config changes applied to RAM, queuing reboot+save\n");
            if (!ota_reboot_with_config_save_request()) {
                UPLOAD_ERROR("u_p_f: /api/config reboot queue failed\n");
                snprintf(response_uri, response_uri_len, "/upload_error.json");
                return;
            }
        } else {
            printf("u_p_f: /api/config - no fields changed\n");
        }

        snprintf(response_uri, response_uri_len, "/upload_success.json");
        return;
    }

    // Handle /api/ecu-live-config POST
    if (ecu_live_config_session.active && ecu_live_config_session.connection == connection) {
        ecu_live_config_session.active = false;
        printf("u_p_f: /api/ecu-live-config body (%zu bytes): %s\n",
               ecu_live_config_session.body_len, ecu_live_config_session.body);

        const char* p = ecu_live_config_session.body;
        p = strstr(p, "\"items\"");
        if (!p) {
            UPLOAD_ERROR("u_p_f: /api/ecu-live-config: no 'items' key\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }
        p = strchr(p, '[');
        if (!p) {
            UPLOAD_ERROR("u_p_f: /api/ecu-live-config: no '['\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }
        p++;

        int16_t items[ECU_LIVE_ITEMS_MAX];
        for (int i = 0; i < ECU_LIVE_ITEMS_MAX; i++) items[i] = -1;

        bool parse_ok = true;
        for (int i = 0; i < ECU_LIVE_ITEMS_MAX; i++) {
            while (*p == ' ' || *p == '\t') p++;
            if (*p == ']' || *p == '\0') break;
            char* endp;
            long val = strtol(p, &endp, 10);
            if (endp == p) { parse_ok = false; break; }
            if (val < -1 || val > 255) { parse_ok = false; break; }
            items[i] = (int16_t)val;
            p = endp;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == ',') p++;
        }

        if (!parse_ok) {
            UPLOAD_ERROR("u_p_f: /api/ecu-live-config: parse error\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }

        // Update RAM immediately
        memcpy(g_ecu_live_items, items, sizeof(g_ecu_live_items));

        // Persist to LittleFS via FileIO task
        file_io_result_t io_result;
        if (!file_io_write_ecu_live_config(items, FILE_IO_TIMEOUT_MS, &io_result)) {
            printf("u_p_f: /api/ecu-live-config: write timed out (RAM updated anyway)\n");
        } else if (!io_result.success) {
            printf("u_p_f: /api/ecu-live-config: write failed: %s (RAM updated anyway)\n",
                   io_result.error_message);
        } else {
            printf("u_p_f: /api/ecu-live-config: saved to /ecu_live.json\n");
        }

        snprintf(response_uri, response_uri_len, "/upload_success.json");
        return;
    }

    // Handle /api/image-store/upload completion
    if (imgstore_upload_session.active && imgstore_upload_session.connection == connection) {
        imgstore_upload_session.active = false;

        if (imgstore_upload_session.overflow ||
            imgstore_upload_session.bytes_received != IMGSTORE_BIN_SIZE) {
            UPLOAD_ERROR("u_p_f: /api/image-store/upload: bad size %lu (expected %d)\n",
                         (unsigned long)imgstore_upload_session.bytes_received, IMGSTORE_BIN_SIZE);
            // Close file if still open, then delete temp file
            file_io_result_t tmp;
            if (imgstore_upload_session.file_open)
                file_io_upload_close(false, FILE_IO_TIMEOUT_MS, &tmp);
            file_io_delete(IMGSTORE_TMP_PATH, FILE_IO_TIMEOUT_MS, &tmp);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }

        // Close/flush the temp file
        file_io_result_t io_result;
        if (!file_io_upload_close(true, FILE_IO_TIMEOUT_MS, &io_result)
                || !io_result.success) {
            UPLOAD_ERROR("u_p_f: /api/image-store/upload: close tmp file failed\n");
            file_io_delete(IMGSTORE_TMP_PATH, FILE_IO_TIMEOUT_MS, &io_result);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }
        imgstore_upload_session.file_open = false;

        // Use specified slot address, or find first empty slot via SWD
        uint32_t slot_addr = imgstore_upload_session.target_slot_addr
                           ? imgstore_upload_session.target_slot_addr
                           : find_empty_slot();
        if (slot_addr == 0) {
            UPLOAD_ERROR("u_p_f: /api/image-store/upload: no empty slot found\n");
            file_io_delete(IMGSTORE_TMP_PATH, FILE_IO_TIMEOUT_MS, &io_result);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }

        printf("u_p_f: /api/image-store/upload: writing '%s' to 0x%08X\n",
               imgstore_upload_session.name, slot_addr);

        // Flash from file — computes murmur3 + builds BSON header internally.
        // Deletes IMGSTORE_TMP_PATH on completion (success or failure).
        bool flash_ok = file_io_flash_ep_slot_from_file(
                            imgstore_upload_session.name,
                            imgstore_upload_session.description,
                            imgstore_upload_session.protection,
                            IMGSTORE_TMP_PATH, slot_addr,
                            60000, &io_result) && io_result.success;
        if (!flash_ok) {
            UPLOAD_ERROR("u_p_f: /api/image-store/upload: flash failed\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }

        printf("u_p_f: /api/image-store/upload: '%s' written to 0x%08X\n",
               imgstore_upload_session.name, slot_addr);
        image_store_invalidate_scan_cache();
        snprintf(response_uri, response_uri_len, "/upload_success.json");
        return;
    }

    // Handle /api/image-store/delete completion
    if (imgstore_delete_session.active && imgstore_delete_session.connection == connection) {
        imgstore_delete_session.active = false;

        uint32_t slot_addr = resolve_slot_addr_from_json(imgstore_delete_session.body);
        if (slot_addr == 0) {
            UPLOAD_ERROR("u_p_f: /api/image-store/delete: slot not found in body: %s\n",
                         imgstore_delete_session.body);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }

        printf("u_p_f: /api/image-store/delete: erasing slot 0x%08X\n", slot_addr);

        file_io_result_t io_result;
        if (!file_io_erase_ep_slot(slot_addr, 60000, &io_result) || !io_result.success) {
            UPLOAD_ERROR("u_p_f: /api/image-store/delete: erase failed\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }

        printf("u_p_f: /api/image-store/delete: slot 0x%08X erased\n", slot_addr);
        image_store_invalidate_scan_cache();
        snprintf(response_uri, response_uri_len, "/upload_success.json");
        return;
    }

    // Handle /api/image-store/selector completion
    if (imgstore_selector_session.active && imgstore_selector_session.connection == connection) {
        imgstore_selector_session.active = false;

        // Parse JSON images array: [{"code":"X"},{"code":"Y","mapblob":"Z"},...]
        // Then build BSON: {images: [{code:"X"},{code:"Y",mapblob:"Z"},...]}
        const char* body = imgstore_selector_session.body;
        uint8_t bson_buf[512] = {};
        BsonWriter w;
        bw_init(&w, bson_buf, sizeof(bson_buf));

        size_t arr_lp = bson_begin_doc(&w, 0x04, "images");
        int entry_count = 0;
        const char* p = strstr(body, "\"images\"");
        if (p) p = strchr(p, '[');
        if (!p) {
            UPLOAD_ERROR("u_p_f: /api/image-store/selector: no images array\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }
        p++;  // skip '['

        while (1) {
            // Skip to next '{'
            while (*p && *p != '{' && *p != ']') p++;
            if (!*p || *p == ']') break;
            p++;  // skip '{'

            // Extract "code" and optional "mapblob" from this entry object
            char code[32] = {0}, mapblob_val[32] = {0}, cont_val[8] = {0};
            // Find end of this object for scoped search
            const char* obj_start = p - 1;
            const char* obj_end = strchr(obj_start, '}');
            if (!obj_end) break;
            // Make a temporary copy to allow json_get_str on a bounded range
            char obj_tmp[96];
            size_t obj_len = (size_t)(obj_end - obj_start + 1);
            if (obj_len >= sizeof(obj_tmp)) obj_len = sizeof(obj_tmp) - 1;
            memcpy(obj_tmp, obj_start, obj_len);
            obj_tmp[obj_len] = '\0';

            if (!json_get_str(obj_tmp, "code", code, sizeof(code)) || code[0] == '\0') {
                p = obj_end + 1;
                continue;
            }
            json_get_str(obj_tmp, "mapblob", mapblob_val, sizeof(mapblob_val));
            json_get_str(obj_tmp, "continue", cont_val, sizeof(cont_val));

            // Write entry as embedded doc with key = string index
            char idx_str[8];
            snprintf(idx_str, sizeof(idx_str), "%d", entry_count);
            size_t entry_lp = bson_begin_doc(&w, 0x03, idx_str);
            bson_write_utf8(&w, "code", code);
            if (mapblob_val[0]) bson_write_utf8(&w, "mapblob", mapblob_val);
            if (cont_val[0])    bson_write_utf8(&w, "continue", cont_val);
            bson_end_doc(&w, entry_lp);

            entry_count++;
            p = obj_end + 1;
        }

        bson_end_doc(&w, arr_lp);
        size_t bson_size = bson_finalise(&w);

        if (bson_size == 0 || entry_count == 0) {
            UPLOAD_ERROR("u_p_f: /api/image-store/selector: BSON build failed or empty\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }

        printf("u_p_f: /api/image-store/selector: writing %d-entry selector (%zu bytes BSON)\n",
               entry_count, bson_size);

        // Write to slot 0 (IMAGE_STORE_BASE)
        const uint32_t IMAGE_STORE_BASE = 0x10200000;
        file_io_result_t io_result;
        if (!file_io_flash_ep_slot(bson_buf, bson_size, nullptr, IMAGE_STORE_BASE,
                                   60000, &io_result) || !io_result.success) {
            UPLOAD_ERROR("u_p_f: /api/image-store/selector: flash failed\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }

        printf("u_p_f: /api/image-store/selector: written, EP rebooting\n");
        image_store_invalidate_selector_cache();
        snprintf(response_uri, response_uri_len, "/upload_success.json");
        return;
    }

    // Handle /api/image-store/edit completion
    if (imgstore_edit_session.active && imgstore_edit_session.connection == connection) {
        imgstore_edit_session.active = false;

        uint32_t slot_addr = 0x10200000u + (uint32_t)imgstore_edit_session.slot_index * 65536u;
        printf("u_p_f: /api/image-store/edit: rewriting header for slot %d (0x%08X) name='%s'\n",
               imgstore_edit_session.slot_index, slot_addr, imgstore_edit_session.name);

        // Reads binary from EP flash via SWD, computes murmur3, builds BSON header —
        // all done inside rewriteSlotHeader() using a 1KB static buffer, no heap.
        file_io_result_t io_result;
        bool flash_ok = file_io_rewrite_ep_slot_header(
                            imgstore_edit_session.name,
                            imgstore_edit_session.description,
                            imgstore_edit_session.protection,
                            slot_addr, 60000, &io_result) && io_result.success;
        if (!flash_ok) {
            UPLOAD_ERROR("u_p_f: /api/image-store/edit: rewriteSlotHeader failed\n");
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }

        printf("u_p_f: /api/image-store/edit: '%s' updated at slot %d\n",
               imgstore_edit_session.name, imgstore_edit_session.slot_index);
        image_store_invalidate_scan_cache();
        snprintf(response_uri, response_uri_len, "/upload_success.json");
        return;
    }

    upload_session_t* session = find_session_by_connection(connection);
    if (!session) {
        UPLOAD_ERROR("upload_post_finished: No session found, conn=%p\n", connection);
        snprintf(response_uri, response_uri_len, "/upload_error.json");
        return;
    }

    // Wait for any pending async write to complete
    if (session->write_in_progress) {
        uint32_t wait_start = to_ms_since_boot(get_absolute_time());

        file_io_result_t result;
        if (!file_io_upload_write_wait(FILE_IO_TIMEOUT_MS, &result)) {
            uint32_t wait_duration = to_ms_since_boot(get_absolute_time()) - wait_start;
            UPLOAD_ERROR("[UPLOAD] Finish TIMEOUT after %lu ms at offset %lu\n",
                   (unsigned long)wait_duration, (unsigned long)session->bytes_received);
            free_session(session);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }
        if (!result.success) {
            UPLOAD_ERROR("[UPLOAD] Finish write FAILED: %s\n", result.error_message);
            free_session(session);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }

        session->write_in_progress = false;
    }

    // Flush any remaining data in active buffer to SD card
    uint8_t* active_buf = (session->active_buffer == 0) ?
                          session->write_buffer_a : session->write_buffer_b;
    uint32_t active_used = session->buffer_used[session->active_buffer];

    if (active_used > 0 && session->file_open) {
        file_io_result_t result;
        if (!file_io_upload_write(active_buf, active_used, FILE_IO_TIMEOUT_MS, &result)) {
            UPLOAD_ERROR("upload_post_finished: Final write timeout (%lu bytes)\n",
                   (unsigned long)active_used);
            free_session(session);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }
        if (!result.success) {
            UPLOAD_ERROR("upload_post_finished: Final write error: %s\n", result.error_message);
            free_session(session);
            snprintf(response_uri, response_uri_len, "/upload_error.json");
            return;
        }
        session->buffer_used[session->active_buffer] = 0;
    }

    bool success = false;
    char sha256_hex[SHA256_RESULT_BYTES * 2 + 1] = "none";

    // Check if upload is complete
    if (session->bytes_received >= session->total_size) {
        // Finalize SHA-256
        if (session->sha_enabled) {
            sha256_result_t sha_result;
            pico_sha256_finish(&session->sha_state, &sha_result);
            session->sha_enabled = false;  // Hardware released, don't finish again in free_session

            // Convert to hex string
            for (int i = 0; i < SHA256_RESULT_BYTES; i++) {
                snprintf(sha256_hex + (i * 2), 3, "%02x", sha_result.bytes[i]);
            }
            sha256_hex[SHA256_RESULT_BYTES * 2] = '\0';
        }

        // Close file (with sync) via file_io_task
        if (session->file_open) {
            file_io_result_t result;
            file_io_upload_close(true, FILE_IO_TIMEOUT_MS, &result);
            session->file_open = false;

            if (!result.success) {
                UPLOAD_ERROR("upload_post_finished: Close error: %s\n", result.error_message);
            }
        }

        printf("upload_post_finished: Upload complete for '%s' (%lu bytes, SHA-256: %.16s...)\n",
               session->filename, (unsigned long)session->bytes_received, sha256_hex);

        success = true;

        // Free session
        free_session(session);
    } else {
        // Only log progress every 5MB to reduce output volume
        static uint32_t last_logged_mb = 0;
        uint32_t current_mb = session->bytes_received / (5 * 1024 * 1024);
        if (current_mb > last_logged_mb) {
            UPLOAD_LOG("upload_post_finished: Progress %.1f MB / %.1f MB\n",
                   session->bytes_received / (1024.0 * 1024.0),
                   session->total_size / (1024.0 * 1024.0));
            last_logged_mb = current_mb;
        }

        // Don't close file yet - more chunks coming
        // Don't free session - keep it for next chunk
        session->last_activity_ms = to_ms_since_boot(get_absolute_time());
    }

    // Generate response
    if (success) {
        snprintf(response_uri, response_uri_len, "/upload_success.json");
        printf("u_p_f: SUCCESS - sending /upload_success.json, conn=%p\n", connection);
    } else {
        snprintf(response_uri, response_uri_len, "/upload_progress.json");
        UPLOAD_LOG("u_p_f: PROGRESS - sending /upload_progress.json, conn=%p, %lu/%lu bytes\n",
                   connection, (unsigned long)session->bytes_received, (unsigned long)session->total_size);
    }
}

void generate_api_upload_session_json(char* buffer, size_t size, const char* session_id)
{
    upload_session_t* session = find_session_by_id(session_id);

    if (!session) {
        snprintf(buffer, size,
                 "{\"error\": \"Session not found\"}");
        return;
    }

    snprintf(buffer, size,
             "{\n"
             "  \"session_id\": \"%s\",\n"
             "  \"filename\": \"%s\",\n"
             "  \"total_size\": %lu,\n"
             "  \"bytes_received\": %lu,\n"
             "  \"next_offset\": %lu\n"
             "}",
             session->session_id,
             session->filename,
             (unsigned long)session->total_size,
             (unsigned long)session->bytes_received,
             (unsigned long)session->bytes_received);
}

// Helper functions

static upload_session_t* find_session_by_connection(void* connection)
{
    for (int i = 0; i < MAX_UPLOAD_SESSIONS; i++) {
        if (upload_sessions[i].in_use && upload_sessions[i].connection == connection) {
            return &upload_sessions[i];
        }
    }
    return NULL;
}

static upload_session_t* find_session_by_id(const char* session_id)
{
    for (int i = 0; i < MAX_UPLOAD_SESSIONS; i++) {
        if (upload_sessions[i].in_use &&
            strcmp(upload_sessions[i].session_id, session_id) == 0) {
            return &upload_sessions[i];
        }
    }
    return NULL;
}

static upload_session_t* allocate_session(void* connection)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());

    for (int i = 0; i < MAX_UPLOAD_SESSIONS; i++) {
        if (!upload_sessions[i].in_use) {
            memset(&upload_sessions[i], 0, sizeof(upload_session_t));

            // Buffers are now statically allocated in the session structure
            // No malloc needed - they're always available

            upload_sessions[i].in_use = true;
            upload_sessions[i].connection = connection;
            upload_sessions[i].last_activity_ms = now;
            return &upload_sessions[i];
        }
    }

    // All slots busy - try to evict the oldest stale session
    int oldest = -1;
    uint32_t oldest_age = 0;
    for (int i = 0; i < MAX_UPLOAD_SESSIONS; i++) {
        uint32_t age = now - upload_sessions[i].last_activity_ms;
        if (age >= UPLOAD_SESSION_TIMEOUT_MS && age > oldest_age) {
            oldest = i;
            oldest_age = age;
        }
    }

    if (oldest >= 0) {
        printf("upload_handler: Evicting stale session %s (idle %lu ms)\n",
               upload_sessions[oldest].session_id, (unsigned long)oldest_age);
        free_session(&upload_sessions[oldest]);
        memset(&upload_sessions[oldest], 0, sizeof(upload_session_t));

        // Buffers are now statically allocated in the session structure
        // No malloc needed - they're always available

        upload_sessions[oldest].in_use = true;
        upload_sessions[oldest].connection = connection;
        upload_sessions[oldest].last_activity_ms = now;
        return &upload_sessions[oldest];
    }

    return NULL;
}

static void free_session(upload_session_t* session)
{
    if (!session) return;

    // Wait for any pending async write to complete before freeing buffers
    if (session->write_in_progress) {
        file_io_result_t result;
        if (file_io_upload_write_wait(FILE_IO_TIMEOUT_MS, &result)) {
            if (!result.success) {
                printf("free_session: Warning: pending write failed: %s\n", result.error_message);
            }
        } else {
            printf("free_session: Warning: pending write timeout\n");
        }
        session->write_in_progress = false;
    }

    // Release SHA-256 hardware if we were using it
    if (session->sha_enabled) {
        sha256_result_t sha_discard;
        pico_sha256_finish(&session->sha_state, &sha_discard);
        session->sha_enabled = false;
    }

    // Close file if still open
    if (session->file_open) {
        file_io_result_t result;
        file_io_upload_close(false, FILE_IO_TIMEOUT_MS, &result);
        session->file_open = false;
    }

    // Buffers are now statically allocated - no free() needed
    // They remain allocated as part of the session structure

    // Mark session as free
    session->in_use = false;
}

static const char* extract_header_value(const char* headers, const char* header_name,
                                        char* value_buffer, size_t buffer_size)
{
    // Simple header parser - finds "Header-Name: value"
    // Thread-safe: Uses caller-provided buffer instead of static buffer
    const char* pos = strstr(headers, header_name);
    if (!pos) return NULL;

    // Skip header name
    pos += strlen(header_name);

    // Skip whitespace
    while (*pos == ' ' || *pos == '\t') pos++;

    // Copy value until CRLF or end of string
    size_t len = 0;
    while (pos[len] != '\r' && pos[len] != '\n' && pos[len] != '\0' && len < buffer_size - 1) {
        value_buffer[len] = pos[len];
        len++;
    }
    value_buffer[len] = '\0';

    return value_buffer;
}

static bool validate_filename(const char* filename)
{
    if (!filename || filename[0] == '\0') {
        return false;
    }

    // Check for path traversal attempts
    if (strchr(filename, '/') || strchr(filename, '\\') ||
        strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
        return false;
    }

    // Check length
    if (strlen(filename) >= 64) {
        return false;
    }

    return true;
}
