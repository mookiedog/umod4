#include "health_gps.h"
#include "wp_rtt.h"
#include <limits.h>

// NAV-PVT is configured at 10 Hz; treat >2 s without one as degraded.
#define GPS_PVT_STALE_MS 2000

static const char *presence_str(GpsPresence p)
{
    switch (p) {
        case GPS_NOT_PRESENT:   return "no";
        case GPS_MAYBE_PRESENT: return "maybe";
        case GPS_PRESENT:       return "yes";
    }
    return "?";
}

static ComponentState s_state = COMP_PENDING;
static Gps* s_gps = nullptr;

static void gps_to_json(void)
{
    if (!s_gps) {
        vfy_printf("{\"state\":\"---\"}");
        return;
    }

    GpsHealth h;
    s_gps->getHealth(&h);

    switch (h.presence) {
        case GPS_NOT_PRESENT:
            s_state = COMP_NA;
            break;
        case GPS_MAYBE_PRESENT:
            s_state = COMP_PENDING;
            break;
        case GPS_PRESENT:
            if (h.nav_pvt_count == 0) {
                s_state = COMP_PENDING;
            }
            else if (h.nav_pvt_age_ms < GPS_PVT_STALE_MS) {
                s_state = COMP_PASS;
            }
            else {
                s_state = COMP_DEGRADED;
            }
            break;
    }

    // Report UINT32_MAX ages as -1 so JSON stays valid (no overflow weirdness).
    int32_t tim_tp_age     = (h.tim_tp_age_ms     == UINT32_MAX) ? -1 : (int32_t)h.tim_tp_age_ms;
    int32_t nav_timels_age = (h.nav_timels_age_ms == UINT32_MAX) ? -1 : (int32_t)h.nav_timels_age_ms;
    int32_t nav_pvt_age    = (h.nav_pvt_age_ms    == UINT32_MAX) ? -1 : (int32_t)h.nav_pvt_age_ms;

    vfy_printf("{\"state\":\"%s\",\"present\":\"%s\","
               "\"rx_err\":%lu,\"cksum_err\":%lu,"
               "\"tim_tp\":%lu,\"nav_timels\":%lu,\"nav_pvt\":%lu,\"unknown\":%lu,"
               "\"tim_tp_age_ms\":%ld,\"nav_timels_age_ms\":%ld,\"nav_pvt_age_ms\":%ld}",
               comp_state_str(s_state),
               presence_str(h.presence),
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

ComponentHealth g_gps_health = { "gps", &s_state, gps_to_json };

void health_gps_init(Gps* gps)
{
    s_gps   = gps;
    s_state = COMP_PENDING;
}
