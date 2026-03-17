#ifndef LOG_META_H
#define LOG_META_H

#include <stdbool.h>

// Metadata for a single log ID entry.
// Entries with name == NULL are undefined/unassigned log IDs.
// Entries with display == false carry data not suitable for a live numeric display
// (e.g. timestamps, string IDs, composite types).
typedef struct {
    const char *name;    // Human-readable name, or NULL if undefined
    const char *units;   // Unit string ("V", "kPa", "°C", ""), or NULL if not displayable
    bool        display; // true = show on live display; false = internal/timing use only
} log_id_meta_t;

// 256-entry table indexed directly by log ID byte.
// Zero-initialised entries (name==NULL, units==NULL, display==false) for all
// undefined IDs — callers must check name != NULL before use.
extern const log_id_meta_t g_log_id_meta[256];

#endif // LOG_META_H
