#include <malloc.h>
#include "pico/critical_section.h"

static critical_section_t heap_crit_sec;
static bool heap_lock_ready = false;

// Initialize the hardware lock at the highest priority, i.e. even before global C++ constructors run
// This allows malloc/free to be used safely in global constructors.
__attribute__((constructor(101)))
void init_malloc_lock() {
    critical_section_init(&heap_crit_sec);
    heap_lock_ready = true;
}

// 2. Supply the hooks Newlib expects
extern "C" {
    void __malloc_lock(struct _reent *r) {
        if (heap_lock_ready) critical_section_enter_blocking(&heap_crit_sec);
    }
    void __malloc_unlock(struct _reent *r) {
        if (heap_lock_ready) critical_section_exit(&heap_crit_sec);
    }
}