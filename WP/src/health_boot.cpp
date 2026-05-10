#include "health_boot.h"
#include "wp_rtt.h"
#include "FlashWp.h"

static ComponentState s_state = COMP_PENDING;
static struct {
    int32_t     boot_slot;
    int32_t     target_slot;
    const char *built;
} s_boot;

static void boot_to_json(void)
{
    vfy_printf("{\"state\":\"%s\",\"slot\":%ld,\"target\":%ld,\"built\":\"%s\"}",
               comp_state_str(s_state),
               (long)s_boot.boot_slot,
               (long)s_boot.target_slot,
               s_boot.built);
}

ComponentHealth g_boot_health = { "boot", &s_state, boot_to_json };

void health_boot_init(void)
{
    s_boot.boot_slot   = FlashWp::get_boot_slot();
    s_boot.target_slot = FlashWp::get_target_slot();
    s_boot.built       = __DATE__ " " __TIME__;
    s_state            = COMP_PASS;
}
