# Log ID Metadata Expansion — Design Plan

## Motivation

The live ECU data webpage (`ecu_live.html`) currently shows raw hex log IDs (e.g. `0x56`) and
raw uint16 integers (e.g. `128`). This is hard to read. Meanwhile, `decodelog.py` already
implements the conversion formulas (Steinhart-Hart for temps, linear for pressure, voltage
divider for VM) but these exist only in Python and are not available to the firmware.

The goal is to add human-readable names, units, and conversion functions as structured metadata
alongside each LOGID definition, then wire that metadata into both the live webpage and the
Python tooling.

---

## Limitations

Live data sounds cool, but what use is it really?
Most of the ECU data stream goes by too fast to be of use to view as a live stream.
A web page might only update every second or two, so viewing things like crankshaft or cam events is pointless.

Slower changing data lends itself to a webpage, but what data might be useful?

Slower Changing Data:

* THW/THA
* AAP, MAP
* Battery voltage
* Trim pot settings
* GPS fix
* GPS time

Diagnostic data

* VTA (for setting TPS)
* Trim pot settings

Error Data:

* Error flag bytes L000C through L000F
* crank and cam error counts (would need to be calculated by rx isr)
* nospark error count

I think what I am learning here is that a general mechanism to select any data from the ecu data stream would be overkill since most of the data is changing too fast to be useful.
It would potentially be easier to implement as a static, predefined set of data to be displayed on the page.

---

## Architecture Overview

```
lib/inc/log_ids.h           ← add _NAME and _UNITS macros per LOGID
lib/inc/log_convert.h       ← NEW: log_id_meta_t struct, g_log_id_meta extern, fn declarations
lib/src/log_convert.c       ← NEW: converter implementations + 256-entry designated-init array
WP/src/api_handlers.cpp     ← use g_log_id_meta to enrich /api/ecu-live-data response
WP/www/ecu_live.html        ← display names + converted values instead of raw hex
tools/src/gen_logmeta.py    ← NEW: parses _NAME/_UNITS macros at build time, emits LogMeta.py
tools/CMakeLists.txt        ← add gen_logmeta.py build step
tools/logtools/decoder/log_convert.py ← NEW: Python converter equivalents (same fn names as C)
tools/logtools/decoder/decodelog.py   ← import from log_convert.py instead of inline defs
```

---

## Key Finding: LOGID_ECU_BASE = 0x00

The ECU events start at **offset** 0x10 (above the GEN range 0x00-0x0F), but
`LOGID_ECU_BASE = 0x00`. Therefore:

- `LOGID_ECU_RAW_VM_TYPE_U8   = (LOGID_ECU_BASE) + 0x56 = 0x56`
- `LOGID_ECU_CAMSHAFT_TYPE_TS = (LOGID_ECU_BASE) + 0x64 = 0x64`

The default live items `[0x56, 0x57, 0x64, 0x66]` correspond to: VM (battery), PORTG_DB,
CAMSHAFT (timestamp — see note below), and one currently-undefined ID.

**Note on 0x64 / CAMSHAFT in the default config**: CAMSHAFT is a `_TS` type (raw 16-bit
timer ticks, meaningless as a standalone value). The default config should probably be updated
to show useful sensor IDs (VM, MAP, THW, THA) instead. The metadata system marks timestamp IDs
as `display: false` so they will show `--` on the live page.

---

## Python Code-Sharing Strategy

The user asked whether it is possible to have "a single C source file that could be included
by Python". **Yes — via ctypes.** The technique is to build `log_convert.c` as a host-arch
shared library (`.so`) and load it with Python's `ctypes` module.

However, the **recommended approach** for now is simpler:

- `log_ids.h` is the single source of truth for **metadata** (names, units).
- `gen_logmeta.py` extracts `_NAME`/`_UNITS` macros at build time and emits `LogMeta.py`
  (a Python dict keyed by log ID integer).
- Python converters live in `tools/logtools/decoder/log_convert.py` with **the same function
  names** as in C (`logconv_ecu_raw_map`, `logconv_ecu_raw_thw`, etc.).

This gives semantic unity (one place for metadata, matching names for converters) without
adding a host-architecture shared library build target to the superbuild. A ctypes upgrade
is straightforward to add later if the Python implementation starts to diverge from C.

---

## Implementation Steps

### 1. `lib/inc/log_ids.h` — Add `_NAME` and `_UNITS` macros

Add two optional macros immediately after each `_DLEN` for displayable sensor IDs:

```c
#define   LOGID_ECU_RAW_VTA_TYPE_U16        ((LOGID_ECU_BASE) + 0x50)   // Throttle angle
#define   LOGID_ECU_RAW_VTA_DLEN            2
#define   LOGID_ECU_RAW_VTA_NAME            "Throttle Position"
#define   LOGID_ECU_RAW_VTA_UNITS           "ADC"

#define   LOGID_ECU_RAW_MAP_TYPE_U8         ((LOGID_ECU_BASE) + 0x52)
#define   LOGID_ECU_RAW_MAP_DLEN            1
#define   LOGID_ECU_RAW_MAP_NAME            "Manifold Pressure"
#define   LOGID_ECU_RAW_MAP_UNITS           "kPa"

// ... similarly for AAP, THW, THA, VM, PORTG_DB, F_IGN_DLY, R_IGN_DLY, WP GPS fields, etc.
```

**Rule**: only add `_NAME`/`_UNITS` to IDs where a numeric live display makes sense.
Do **not** add them to `_TS`, `_PTS`, `_V`, or `_CS` types.

**Constraint preserved**: file remains `#define`-only (required for assembly + Python compatibility).

---

### 2. `lib/inc/log_convert.h` — New file

```c
#ifndef LOG_CONVERT_H
#define LOG_CONVERT_H
#include <stdint.h>
#include <stdbool.h>

typedef float (*log_convert_fn_t)(uint16_t raw);

typedef struct {
    const char         *name;     // NULL if this log ID is not defined
    const char         *units;    // "" if dimensionless, NULL if undefined
    log_convert_fn_t    convert;  // NULL = display the raw integer value
    bool                display;  // false = not suitable for live display
} log_id_meta_t;

// Global 256-entry table indexed by log ID byte
extern const log_id_meta_t g_log_id_meta[256];

// Converter function declarations
float logconv_ecu_raw_vta(uint16_t raw);  // strips timer bits → 10-bit ADC
float logconv_ecu_raw_map(uint16_t raw);  // ADC → kPa
float logconv_ecu_raw_aap(uint16_t raw);  // ADC → kPa (same formula as MAP)
float logconv_ecu_raw_thw(uint16_t raw);  // ADC → °C  (Steinhart-Hart NTC)
float logconv_ecu_raw_tha(uint16_t raw);  // ADC → °C  (same formula as THW)
float logconv_ecu_raw_vm(uint16_t raw);   // ADC → V   (voltage divider ×4)
float logconv_ecu_ign_dly(uint16_t raw);  // 0.8 fixed-point → ignition advance °
#endif
```

---

### 3. `lib/src/log_convert.c` — New file

```c
#include "log_convert.h"
#include "log_ids.h"
#include <math.h>

// --- Converter implementations ---

float logconv_ecu_raw_vta(uint16_t raw) { return (float)(raw & 0x3FFu); }

float logconv_ecu_raw_map(uint16_t raw) {
    float Vo = (raw / 256.0f) * 5.0f;
    return (Vo - 0.6f) / 0.03f;  // Aprilia MAP/AAP sensor linear formula
}
float logconv_ecu_raw_aap(uint16_t raw) { return logconv_ecu_raw_map(raw); }

float logconv_ecu_raw_thw(uint16_t raw) {
    float Vmeas = raw * 5.0f / 255.0f;
    float Rntc  = (Vmeas * 2700.0f) / (5.0f - Vmeas);
    float logR  = logf(Rntc);
    float A = 1.142579776e-3f, B = 2.941596847e-4f, C = -0.5305974726e-7f;
    return 1.0f / (A + B*logR + C*logR*logR*logR) - 273.15f;
}
float logconv_ecu_raw_tha(uint16_t raw) { return logconv_ecu_raw_thw(raw); }

float logconv_ecu_raw_vm(uint16_t raw)  { return (raw / 256.0f) * 5.0f * 4.0f; }
float logconv_ecu_ign_dly(uint16_t raw) { return (raw / 256.0f) * 90.0f - 18.0f; }

// --- Helper macros for table entries ---
#define META_CONV(n,u,fn)  {(n),(u),(fn),true}
#define META_RAW(n,u)      {(n),(u),NULL,true}
#define META_NODISPLAY(n)  {(n),NULL,NULL,false}
// Unspecified entries are zero-initialized → {NULL,NULL,NULL,false}

// --- 256-entry metadata table using C99 designated initializers ---
const log_id_meta_t g_log_id_meta[256] = {

    // GEN
    [LOGID_GEN_ECU_LOG_VER_TYPE_U8]  = META_NODISPLAY("ECU Log Version"),
    [LOGID_GEN_EP_LOG_VER_TYPE_U8]   = META_NODISPLAY("EP Log Version"),
    [LOGID_GEN_WP_LOG_VER_TYPE_U8]   = META_NODISPLAY("WP Log Version"),

    // ECU — events / timestamps: not suitable for live display
    [LOGID_ECU_CPU_EVENT_TYPE_U8]      = META_NODISPLAY("CPU Event"),
    [LOGID_ECU_T1_OFLO_TYPE_TS]        = META_NODISPLAY("Timer Overflow"),
    [LOGID_ECU_F_INJ_ON_TYPE_PTS]      = META_NODISPLAY("Front Inj ON"),
    [LOGID_ECU_R_INJ_ON_TYPE_PTS]      = META_NODISPLAY("Rear Inj ON"),
    [LOGID_ECU_F_COIL_ON_TYPE_PTS]     = META_NODISPLAY("Front Coil ON"),
    [LOGID_ECU_F_COIL_OFF_TYPE_PTS]    = META_NODISPLAY("Front Coil OFF"),
    [LOGID_ECU_R_COIL_ON_TYPE_PTS]     = META_NODISPLAY("Rear Coil ON"),
    [LOGID_ECU_R_COIL_OFF_TYPE_PTS]    = META_NODISPLAY("Rear Coil OFF"),
    [LOGID_ECU_CRANKREF_START_TYPE_TS] = META_NODISPLAY("Crank Ref Start"),
    [LOGID_ECU_CAMSHAFT_TYPE_TS]       = META_NODISPLAY("Camshaft Event"),
    [LOGID_ECU_SPRK_X1_TYPE_PTS]       = META_NODISPLAY("Spark X1"),
    [LOGID_ECU_SPRK_X2_TYPE_PTS]       = META_NODISPLAY("Spark X2"),

    // ECU — durations and counts: displayable as raw values
    [LOGID_ECU_F_INJ_DUR_TYPE_U16]     = META_RAW("Front Inj Duration", "ticks"),
    [LOGID_ECU_R_INJ_DUR_TYPE_U16]     = META_RAW("Rear Inj Duration",  "ticks"),
    [LOGID_ECU_FUEL_PUMP_TYPE_B]        = META_RAW("Fuel Pump",          ""),
    [LOGID_ECU_CRANKREF_ID_TYPE_U8]     = META_RAW("Crank Ref ID",       ""),
    [LOGID_ECU_CAM_ERR_TYPE_U8]         = META_RAW("CAM Error",          ""),
    [LOGID_ECU_NOSPARK_TYPE_U8]         = META_RAW("No Spark",           ""),

    // ECU — ignition timing: has converter
    [LOGID_ECU_F_IGN_DLY_TYPE_0P8]     = META_CONV("Front Ign Advance", "deg", logconv_ecu_ign_dly),
    [LOGID_ECU_R_IGN_DLY_TYPE_0P8]     = META_CONV("Rear Ign Advance",  "deg", logconv_ecu_ign_dly),

    // ECU — sensor readings: have converters
    [LOGID_ECU_RAW_VTA_TYPE_U16]  = META_CONV("Throttle Position", "ADC",  logconv_ecu_raw_vta),
    [LOGID_ECU_RAW_MAP_TYPE_U8]   = META_CONV("Manifold Pressure",  "kPa", logconv_ecu_raw_map),
    [LOGID_ECU_RAW_AAP_TYPE_U8]   = META_CONV("Ambient Pressure",   "kPa", logconv_ecu_raw_aap),
    [LOGID_ECU_RAW_THW_TYPE_U8]   = META_CONV("Coolant Temp",       "°C",  logconv_ecu_raw_thw),
    [LOGID_ECU_RAW_THA_TYPE_U8]   = META_CONV("Air Temp",           "°C",  logconv_ecu_raw_tha),
    [LOGID_ECU_RAW_VM_TYPE_U8]    = META_CONV("Battery Voltage",    "V",   logconv_ecu_raw_vm),
    [LOGID_ECU_PORTG_DB_TYPE_U8]  = META_RAW ("Port G",             ""),

    // EP
    [LOGID_EP_LOAD_ERR_TYPE_U8]        = META_RAW("EP Load Error",  ""),
    [LOGID_EP_LOADED_SLOT_TYPE_U8]     = META_RAW("EP Loaded Slot", ""),

    // WP
    [LOGID_WP_CSECS_TYPE_U8]       = META_RAW("Centiseconds",  "cs"),
    [LOGID_WP_SECS_TYPE_U8]        = META_RAW("Seconds",       "s"),
    [LOGID_WP_MINS_TYPE_U8]        = META_RAW("Minutes",       "min"),
    [LOGID_WP_HOURS_TYPE_U8]       = META_RAW("Hours",         "h"),
    [LOGID_WP_FIXTYPE_TYPE_U8]     = META_RAW("GPS Fix Type",  ""),
    [LOGID_WP_GPS_VELO_TYPE_U16]   = META_RAW("GPS Speed",     "mm/s"),
    [LOGID_WP_WR_TIME_TYPE_U16]    = META_RAW("FS Write Time", "ms"),
    [LOGID_WP_SYNC_TIME_TYPE_U16]  = META_RAW("FS Sync Time",  "ms"),
    // LOGID_WP_GPS_POSN (8-byte composite): NODISPLAY / excluded
};
```

---

### 4. `WP/CMakeLists.txt`

In the `add_executable(${PROJECT_NAME} ...)` block (around line 191), add:

```cmake
${LIB_DIR}/src/log_convert.c
```

`${LIB_DIR}/inc` is already on the include path. Verify `logf()` links — the Pico SDK
provides optimized float math; add `m` to `target_link_libraries` explicitly if needed.

---

### 5. `WP/src/api_handlers.cpp`

- Add `#include "log_convert.h"` at the top.
- Increase `json_response_buffer` from 512 → 1024 bytes (10 slots × ~80 bytes/slot).
- Rewrite `generate_api_ecu_live_data_json()` to produce:

```json
{"items":[
  {"slot":0,"logid":86,"name":"Battery Voltage","units":"V","raw":191,"value":14.9},
  {"slot":1,"logid":87,"name":"Port G","units":"","raw":0,"value":0.0},
  {"slot":4,"logid":-1,"name":null,"units":null,"raw":null,"value":null}
]}
```

Logic per slot:
```c
const log_id_meta_t *m = &g_log_id_meta[(uint8_t)logid];
uint16_t raw = ecuLiveLog[logid];
float display_val = m->convert ? m->convert(raw) : (float)raw;
// snprintf with name/units/raw/value; use "null" for slots with logid < 0
// or for display:false entries, set value to null
```

---

### 6. `WP/www/ecu_live.html`

Updated per-slot HTML structure:

```html
<div class="status-item">
  <div class="status-name" id="name-N">Battery Voltage</div>
  <div class="status-value">
    <span id="val-N">14.9</span>
    <span class="status-units" id="units-N"> V</span>
  </div>
  <div class="status-id">0x56</div>
</div>
```

Update `pollData()`:

```javascript
el_name.textContent  = entry.name  || hex_label;
el_val.textContent   = (entry.value !== null) ? entry.value.toFixed(2) : '--';
el_units.textContent = entry.units || '';
```

Edit mode is unchanged — users still enter log IDs as hex or decimal numbers.

---

### 7. `tools/src/gen_logmeta.py` — New build-time script

Parses `log_ids.h` for all `*_NAME` and `*_UNITS` macros (string literals), cross-references
with the generated `Logsyms.py` to resolve numeric IDs, and emits `LogMeta.py`:

```python
# Auto-generated by gen_logmeta.py — do not edit
LOG_ID_META = {
    0x56: ("Battery Voltage", "V"),
    0x62: ("Manifold Pressure", "kPa"),
    0x64: ("Coolant Temp", "°C"),
    # ...
}
```

---

### 8. `tools/CMakeLists.txt`

Add a custom command that runs `gen_logmeta.py` after `h2py.py` completes (so `Logsyms.py`
is a declared dependency), and install `LogMeta.py` to site-packages alongside `Logsyms.py`.

---

### 9. `tools/logtools/decoder/log_convert.py` — New Python module

```python
"""
Python equivalents of lib/src/log_convert.c converter functions.
Function names intentionally match the C implementations for semantic parity.
"""
import math

def logconv_ecu_raw_vta(raw):
    return float(raw & 0x3FF)

def logconv_ecu_raw_map(raw):
    Vo = (raw / 256.0) * 5.0
    return (Vo - 0.6) / 0.03

def logconv_ecu_raw_aap(raw):
    return logconv_ecu_raw_map(raw)

def logconv_ecu_raw_thw(raw):
    Vmeas = raw * 5.0 / 255.0
    Rntc = (Vmeas * 2700.0) / (5.0 - Vmeas)
    logR = math.log(Rntc)
    A, B, C = 1.142579776e-3, 2.941596847e-4, -0.5305974726e-7
    return 1.0 / (A + B*logR + C*logR**3) - 273.15

def logconv_ecu_raw_tha(raw):
    return logconv_ecu_raw_thw(raw)

def logconv_ecu_raw_vm(raw):
    return (raw / 256.0) * 5.0 * 4.0

def logconv_ecu_ign_dly(raw):
    return (raw / 256.0) * 90.0 - 18.0
```

The existing inline conversions in `decodelog.py` are moved to this module and imported.

---

## Files Summary

| File | Action |
|------|--------|
| `lib/inc/log_ids.h` | Add `_NAME` / `_UNITS` macros to ~15 sensor IDs |
| `lib/inc/log_convert.h` | **CREATE** — struct, extern array, fn declarations |
| `lib/src/log_convert.c` | **CREATE** — implementations + 256-entry table |
| `WP/CMakeLists.txt` | Add `log_convert.c` to sources |
| `WP/src/api_handlers.cpp` | Enlarge buffer, include header, enrich JSON response |
| `WP/www/ecu_live.html` | Add name/units display, update JS poll function |
| `tools/src/gen_logmeta.py` | **CREATE** — generates `LogMeta.py` at build time |
| `tools/CMakeLists.txt` | Add `gen_logmeta.py` build step + site-packages install |
| `tools/logtools/decoder/log_convert.py` | **CREATE** — Python converter equivalents |
| `tools/logtools/decoder/decodelog.py` | Import converters from `log_convert.py` |

---

## Deferred / Out of Scope

- **C-string IDs** (`_CS` type): accumulated multi-byte, incompatible with the uint16 interface.
  Acknowledged by user; may be handled separately via a WP ISR extension.
- **GPS position** (`LOGID_WP_GPS_POSN`, 8-byte composite): can't fit in `uint16_t ecuLiveLog`.
- **Timestamp IDs** (`_TS`, `_PTS`): raw value is a fraction of a 131 ms timer cycle —
  meaningless as a standalone number. These are marked `display: false`.
- **ctypes shared library**: viable future path if Python and C implementations diverge.

---

## Verification Steps

1. `cd ~/projects/umod4/build/WP && ninja` — WP builds clean
2. `cd ~/projects/umod4/build && ninja` — `LogMeta.py` appears in tools site-packages
3. Flash WP; open `http://192.168.4.1/ecu_live.html` — slots show names and converted values
4. `build/.venv/bin/python3 -c "from Logsyms import LogMeta; print(LogMeta.LOG_ID_META)"`
5. Decode a log file; verify converted values match existing HDF5 output from `decodelog.py`
