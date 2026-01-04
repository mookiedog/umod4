#include <malloc.h>
#include <stdlib.h>
#include "hardware/sync.h"
#include "pico/platform.h"

static spin_lock_t *heap_lock;
static bool heap_lock_ready = false;

// Per-core storage for saved interrupt state
// Each core needs its own because both can be in malloc simultaneously
static uint32_t heap_irq_save[2];

// Initialize hardware spinlock before anything else
__attribute__((constructor(101)))
void init_malloc_lock() {
    // Claim an unused spinlock from the hardware pool
    heap_lock = spin_lock_init(spin_lock_claim_unused(true));
    heap_lock_ready = true;
}

extern "C" {
    void __malloc_lock(struct _reent *r) {
        if (heap_lock_ready) {
            // Get current core number (0 or 1)
            uint core_num = get_core_num();

            // Hardware spinlock WITH interrupt disable
            // This prevents task preemption AND ISR preemption while holding the lock
            heap_irq_save[core_num] = spin_lock_blocking(heap_lock);
        }
    }

    void __malloc_unlock(struct _reent *r) {
        if (heap_lock_ready) {
            // Get current core number (0 or 1)
            uint core_num = get_core_num();

            // Restore interrupts to their prior state and release spinlock
            spin_unlock(heap_lock, heap_irq_save[core_num]);
        }
    }

    // FreeRTOS heap functions - just call malloc/free directly
    // (malloc/free already protected by __malloc_lock above)
    void* pvPortMalloc(size_t xWantedSize) {
        return malloc(xWantedSize);
    }

    void vPortFree(void* pv) {
        free(pv);
    }
}
