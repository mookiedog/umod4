#include "task_stats.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdlib.h>

// FreeRTOS accumulates per-task runtime into configRUN_TIME_COUNTER_TYPE (uint64_t)
// natively at every context switch. We just copy the values out here — no external
// tracking or delta math needed.

static task_stat_t s_stats[TASK_STATS_MAX_TASKS];
static int         s_stats_count = 0;
static uint64_t    s_total_us    = 0;

static int cmp_runtime_desc(const void* a, const void* b)
{
    uint64_t ra = ((const task_stat_t*)a)->runtime_us;
    uint64_t rb = ((const task_stat_t*)b)->runtime_us;
    return (rb > ra) ? 1 : (rb < ra) ? -1 : 0;
}

int task_stats_update(void)
{
    static TaskStatus_t raw[TASK_STATS_MAX_TASKS];
    uint64_t total_us = 0;
    UBaseType_t count = uxTaskGetSystemState(raw, TASK_STATS_MAX_TASKS, &total_us);

    for (UBaseType_t i = 0; i < count; i++) {
        s_stats[i].handle        = raw[i].xHandle;
        s_stats[i].runtime_us    = raw[i].ulRunTimeCounter;
        s_stats[i].state         = raw[i].eCurrentState;
        s_stats[i].stack_hwm     = raw[i].usStackHighWaterMark;
        s_stats[i].core_affinity = raw[i].uxCoreAffinityMask;
        strncpy(s_stats[i].name, raw[i].pcTaskName, configMAX_TASK_NAME_LEN - 1);
        s_stats[i].name[configMAX_TASK_NAME_LEN - 1] = '\0';
    }
    s_stats_count = (int)count;
    s_total_us    = total_us;

    qsort(s_stats, s_stats_count, sizeof(task_stat_t), cmp_runtime_desc);
    return s_stats_count;
}

const task_stat_t* task_stats_get(int* count_out)
{
    if (count_out) *count_out = s_stats_count;
    return s_stats;
}

uint64_t task_stats_get_total(void)
{
    return s_total_us;
}
