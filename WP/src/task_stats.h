#pragma once
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#define TASK_STATS_MAX_TASKS 32

typedef struct {
    TaskHandle_t handle;
    char         name[configMAX_TASK_NAME_LEN];
    eTaskState   state;
    uint16_t     stack_hwm;
    UBaseType_t  core_affinity;
    uint64_t     runtime_us;    // 64-bit accumulated microseconds
} task_stat_t;

// Snapshot FreeRTOS runtime counters (64-bit, via configRUN_TIME_COUNTER_TYPE).
// Thread-safe. Returns task count.
int task_stats_update(void);

// Read-only pointer to the snapshot array, sorted descending by runtime_us.
// Valid until the next task_stats_update() call.
const task_stat_t* task_stats_get(int* count_out);

// 64-bit sum of all tasks' runtime_us from the last task_stats_update() call.
uint64_t task_stats_get_total(void);
