#pragma once
#include <stdint.h>

// Per-component health states. Each component reports one of these.
// The health VFY command returns all component states as JSON facts —
// the test harness decides what constitutes pass or fail for its context.
typedef enum {
    COMP_PASS,      // nominal
    COMP_DEGRADED,  // functional but sub-nominal
    COMP_FAIL,      // unrecoverable error
    COMP_NA,        // hardware not present
    COMP_DISABLED,  // turned off in config
    COMP_PENDING,   // still initialising, verdict not yet available
} ComponentState;

// Each component's to_json() writes its JSON fragment directly to the VFY
// channel via vfy_printf(). No intermediate buffer — no size ceiling.
typedef void (*health_json_fn)(void);

typedef struct {
    const char     *name;
    ComponentState *state;    // read without parsing JSON for aggregate logic
    health_json_fn  to_json;  // called on demand when "health" command fires
} ComponentHealth;

// Map a ComponentState to its JSON string value.
const char *comp_state_str(ComponentState s);

// Walk the registered component list and write the full health JSON to the
// VFY channel. Called by VfyTask in response to the "health" command.
// pretty=true: one component per line, indented — for interactive RTT use.
// pretty=false: single line — for the test harness.
void health_report(bool pretty);
