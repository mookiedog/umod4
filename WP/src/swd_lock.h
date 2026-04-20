#pragma once
#include "FreeRTOS.h"
#include "semphr.h"

// Recursive mutex protecting all access to the shared `swd` object.
// Must be created (xSemaphoreCreateRecursiveMutex) in main() before any
// task that uses SWD is started.
extern SemaphoreHandle_t g_swd_mutex;

// RAII guard: acquires g_swd_mutex on construction, releases on destruction.
// Recursive so nested acquisitions within the same task are safe
// (e.g. flashUf2 → process_uf2 → flashSlot all taking the lock).
class SWDLock {
public:
    SWDLock()  { xSemaphoreTakeRecursive(g_swd_mutex, portMAX_DELAY); }
    ~SWDLock() { xSemaphoreGiveRecursive(g_swd_mutex); }
    SWDLock(const SWDLock&) = delete;
    SWDLock& operator=(const SWDLock&) = delete;
};
