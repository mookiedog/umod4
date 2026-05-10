#include "health_fs.h"
#include "wp_rtt.h"

static ComponentState s_state       = COMP_PENDING;
static bool           s_reformatted = false;
static uint32_t       s_mount_ms    = 0;

static void fs_to_json(void)
{
    vfy_printf("{\"state\":\"%s\",\"reformatted\":%s,\"mount_ms\":%lu}",
               comp_state_str(s_state),
               s_reformatted ? "true" : "false",
               (unsigned long)s_mount_ms);
}

ComponentHealth g_fs_health = { "fs", &s_state, fs_to_json };

void health_fs_set_mounted(bool reformatted, uint32_t mount_time_ms)
{
    s_reformatted = reformatted;
    s_mount_ms    = mount_time_ms;
    s_state       = COMP_PASS;
}

void health_fs_set_unmounted(void)
{
    s_state = COMP_FAIL;
}
