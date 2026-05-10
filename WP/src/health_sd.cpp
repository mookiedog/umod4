#include "health_sd.h"
#include "wp_rtt.h"

static SdCardBase    *s_card  = nullptr;
static ComponentState s_state = COMP_PENDING;

static const char *hw_state_str(SdCardBase::state_t s)
{
    switch (s) {
        case SdCardBase::NO_CARD:     return "no_card";
        case SdCardBase::MAYBE_CARD:  return "maybe_card";
        case SdCardBase::POWER_UP:    return "power_up";
        case SdCardBase::INIT_CARD:   return "init_card";
        case SdCardBase::VERIFYING:   return "verifying";
        case SdCardBase::OPERATIONAL: return "operational";
        default:                      return "unknown";
    }
}

static void sd_to_json(void)
{
    if (!s_card) {
        s_state = COMP_PENDING;
        vfy_printf("{\"state\":\"---\",\"hw\":\"not_started\"}");
        return;
    }
    s_state = (s_card->state == SdCardBase::OPERATIONAL) ? COMP_PASS : COMP_PENDING;
    vfy_printf("{\"state\":\"%s\",\"hw\":\"%s\",\"size_mb\":%lu}",
               comp_state_str(s_state),
               hw_state_str(s_card->state),
               (unsigned long)s_card->getCapacityMB());
}

ComponentHealth g_sd_health = { "sd", &s_state, sd_to_json };

void health_sd_init(SdCardBase *card)
{
    s_card = card;
}
