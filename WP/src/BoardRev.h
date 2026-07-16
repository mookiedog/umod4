#ifndef BOARDREV_H
#define BOARDREV_H

// PCB revision, detected at boot from a deliberate bodge resistor on DISK_BSY_PIN (GP26).
// See BoardRev.cpp for the detection method and margin analysis.
typedef enum {
    PCB_REV_UNKNOWN,    // boardrev_detect() hasn't run yet
    PCB_REV_4V1,
    PCB_REV_4V2,
} PcbRev;

// Probes the board-ID strap and caches the result. Call once, at the very start of main(),
// before anything else touches DISK_BSY_PIN (e.g. hello()'s boot blink), since its active
// level depends on the result of this detection.
PcbRev boardrev_detect(void);

// Returns the cached result of the last boardrev_detect() call (PCB_REV_UNKNOWN if it hasn't run yet).
PcbRev boardrev_get(void);

// Returns the cached board revision as a string: "Unknown", "4V1", or "4V2".
const char* boardrev_str(void);

#endif
