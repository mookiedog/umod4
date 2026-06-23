#include "Trace.h"
#include "lfsMgr.h"
#include "wp_rtt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define TRACE_PRINTF(fmt, ...) shell_printf(fmt, ##__VA_ARGS__)

static const char* TRACE_CONFIG_FILE = "/trace.cfg";

static TraceEntry s_entries[TRACE_MAX_ENTRIES];
static int s_count = 0;

// ----------------------------------------------------------------------------------
void Trace::reg(const char* name, uint8_t* level)
{
    if (s_count >= TRACE_MAX_ENTRIES) {
        printf("Trace: registry full, cannot add '%s'\n", name);
        return;
    }
    s_entries[s_count].name = name;
    s_entries[s_count].level = level;
    s_count++;
}

// ----------------------------------------------------------------------------------
int Trace::getCount()
{
    return s_count;
}

// ----------------------------------------------------------------------------------
const TraceEntry* Trace::getEntries()
{
    return s_entries;
}

// ----------------------------------------------------------------------------------
static int findEntry(const char* name)
{
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_entries[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// ----------------------------------------------------------------------------------
static uint8_t parseLevel(const char* value)
{
    if (strcmp(value, "on") == 0 || strcmp(value, "true") == 0) {
        return 1;
    }
    if (strcmp(value, "off") == 0 || strcmp(value, "false") == 0) {
        return 0;
    }
    return (uint8_t)strtoul(value, nullptr, 10);
}

// ----------------------------------------------------------------------------------
static void showAll()
{
    TRACE_PRINTF("Trace entries (%d registered):\n", s_count);
    for (int i = 0; i < s_count; i++) {
        TRACE_PRINTF("  %-12s %u\n", s_entries[i].name, *s_entries[i].level);
    }
}

// ----------------------------------------------------------------------------------
static void showOne(int idx)
{
    TRACE_PRINTF("  %-12s %u\n", s_entries[idx].name, *s_entries[idx].level);
}

// ----------------------------------------------------------------------------------
static void applyLine(const char* line)
{
    while (*line && isspace(*line)) line++;
    if (*line == '\0' || *line == '#') return;

    char buf[64];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // Strip trailing whitespace / newline
    char* end = buf + strlen(buf) - 1;
    while (end >= buf && isspace(*end)) {
        *end-- = '\0';
    }

    char* eq = strchr(buf, '=');
    if (!eq) return;

    *eq++ = '\0';
    const char* name = buf;
    const char* value = eq;

    int idx = findEntry(name);
    if (idx >= 0) {
        *s_entries[idx].level = parseLevel(value);
    } else {
        printf("Trace: config: unknown entry '%s'\n", name);
    }
}

// ----------------------------------------------------------------------------------
void Trace::loadConfig()
{
    if (!lfs_mounted) return;

    lfs_file_t f;
    int err = lfs_file_open(&lfs, &f, TRACE_CONFIG_FILE, LFS_O_RDONLY);
    if (err != LFS_ERR_OK) return;

    char line[64];
    int pos = 0;
    char c;

    while (lfs_file_read(&lfs, &f, &c, 1) == 1) {
        if (c == '\n' || c == '\r') {
            line[pos] = '\0';
            if (pos > 0) applyLine(line);
            pos = 0;
        } else if (pos < (int)sizeof(line) - 1) {
            line[pos++] = c;
        }
    }

    if (pos > 0) {
        line[pos] = '\0';
        applyLine(line);
    }

    lfs_file_close(&lfs, &f);
    printf("Trace: loaded %s\n", TRACE_CONFIG_FILE);
}

// ----------------------------------------------------------------------------------
void Trace::saveConfig()
{
    if (!lfs_mounted) {
        TRACE_PRINTF("Trace: filesystem not mounted\n");
        return;
    }

    lfs_file_t f;
    int err = lfs_file_open(&lfs, &f, TRACE_CONFIG_FILE,
                            LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err != LFS_ERR_OK) {
        TRACE_PRINTF("Trace: failed to create %s (%d)\n", TRACE_CONFIG_FILE, err);
        return;
    }

    char line[64];
    for (int i = 0; i < s_count; i++) {
        int len = snprintf(line, sizeof(line), "%s=%u\n",
                           s_entries[i].name, *s_entries[i].level);
        lfs_file_write(&lfs, &f, line, len);
    }

    lfs_file_close(&lfs, &f);
    TRACE_PRINTF("Trace: saved to %s\n", TRACE_CONFIG_FILE);
}

// ----------------------------------------------------------------------------------
void Trace::deleteConfig()
{
    if (!lfs_mounted) {
        TRACE_PRINTF("Trace: filesystem not mounted\n");
        return;
    }

    int err = lfs_remove(&lfs, TRACE_CONFIG_FILE);
    if (err == LFS_ERR_OK) {
        TRACE_PRINTF("Trace: deleted %s\n", TRACE_CONFIG_FILE);
    } else if (err == LFS_ERR_NOENT) {
        TRACE_PRINTF("Trace: %s does not exist\n", TRACE_CONFIG_FILE);
    } else {
        TRACE_PRINTF("Trace: failed to delete %s (%d)\n", TRACE_CONFIG_FILE, err);
    }
}

// ----------------------------------------------------------------------------------
void Trace::cmdTrace(char* args)
{
    if (!args || *args == '\0') {
        showAll();
        return;
    }

    // Skip leading whitespace
    while (*args && isspace(*args)) args++;

    if (strcmp(args, "save") == 0) {
        saveConfig();
        return;
    }

    if (strcmp(args, "delete") == 0) {
        deleteConfig();
        return;
    }

    // Check for name=value
    char* eq = strchr(args, '=');
    if (eq) {
        *eq++ = '\0';
        int idx = findEntry(args);
        if (idx < 0) {
            TRACE_PRINTF("Trace: unknown entry '%s'\n", args);
            return;
        }
        *s_entries[idx].level = parseLevel(eq);
        showOne(idx);
        return;
    }

    // Just a name — show its current value
    int idx = findEntry(args);
    if (idx < 0) {
        TRACE_PRINTF("Trace: unknown entry '%s'\n", args);
        return;
    }
    showOne(idx);
}
