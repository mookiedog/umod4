#include "wp_health.h"
#include "wp_rtt.h"
#include "health_boot.h"
#include "health_sd.h"
#include "health_fs.h"
#include "health_gps.h"

const char *comp_state_str(ComponentState s)
{
    switch (s) {
        case COMP_PASS:     return "pass";
        case COMP_DEGRADED: return "degraded";
        case COMP_FAIL:     return "fail";
        case COMP_NA:       return "na";
        case COMP_DISABLED: return "disabled";
        case COMP_PENDING:  return "pending";
        default:            return "unknown";
    }
}

static ComponentHealth *s_components[] = {
    &g_boot_health,
    &g_sd_health,
    &g_fs_health,
    &g_gps_health,
};
static const int NUM_COMPONENTS = (int)(sizeof(s_components) / sizeof(s_components[0]));

void health_report(bool pretty)
{
    if (pretty) {
        vfy_printf("{\"health\":{\n");
        for (int i = 0; i < NUM_COMPONENTS; i++) {
            ComponentHealth *c = s_components[i];
            vfy_printf("  \"%s\":", c->name);
            c->to_json();
            vfy_printf((i < NUM_COMPONENTS - 1) ? ",\n" : "\n");
        }
        vfy_printf("}}\n");
    } else {
        vfy_printf("{\"health\":{");
        for (int i = 0; i < NUM_COMPONENTS; i++) {
            ComponentHealth *c = s_components[i];
            if (i > 0) vfy_printf(",");
            vfy_printf("\"%s\":", c->name);
            c->to_json();
        }
        vfy_printf("}}\n");
    }
}
