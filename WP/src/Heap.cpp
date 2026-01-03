// This file serves as a Heap manager for C, C++, and FreeRTOS.
// It provides implementations for malloc(), free(), new(), and delete() as well as
// FreeRTOS allocation and deallocation functions.
//
// In a newlib-based system like this one, the heap is defined to start right after the
// RAM BSS section ends. Specifically, the linker .ld file will define a global symbol
// called 'end' which marks the end of BSS RAM.
// In normal circumstances, the heap will use all the RAM between the end of BSS and
// the bottom of the stack. However, the stack used at startup starts at the end of RAM
// and goes downwards, meaning that it is using the heap area.
// In a FreeRTOS system, this is a minor and temporary problem since that initial stack
// in high RAM is only used to get FreeRTOS started. After the scheduler starts,
// all of the stacks in the system exist inside RAM that was malloc'd (from the heap)
// at the time when the task was created. So we ignore it.
//
// Multiprocessing and the Heap
// This processor is dual core, so both cores could potentially make allocation calls
// simultaneously.
// The FreeRTOS enterCriticalSection() call disables interrupts. This has the desirable
// effect of preventing a task switch while inside a critical section because task switching
// only occurs inside an interrupt.
// But it only disables interrupts on the calling core. A second core could


#include "pico.h"
#include "stdlib.h"
#include "string.h"

#include "FreeRTOS.h"
#include "task.h"
#include "Heap.h"

// Linker-provided symbols:
extern uint32_t __end__;        // The end of BSS, and the start of the heap
extern uint32_t __HeapLimit;    // The end of the heap

const char* heap_start = (char*)&__end__;
const char* heap_end = (char*)&__HeapLimit;

// --------------------------------------------------------------------------------
// For now, we will be using newlib's malloc() and free(), but we will supply our
// own _sbrk() for the newlib version of malloc() to call when it needs RAM.
//
extern "C" void *_sbrk(ptrdiff_t incr);
void *_sbrk(ptrdiff_t incr)
{
  // This pointer will always be double-word aligned.
  static char* brk;

  if (incr == 0) {
    return brk;
  }

  taskENTER_CRITICAL();

  // All requests > 0 bytes get their length rounded up so that the brk pointer remains double-word aligned.
  ptrdiff_t aligned_incr = (incr+7) & ~0x7;

  // If this is the first time here, init our brk pointer to the start of the heap,
  // forcing it to a double-word alignment:
  if (!brk) {
    brk = (char*)(((uint32_t)(heap_start) + 7) & ~0x7);
  }


  char *old_brk = brk;

  if ((brk + incr) < heap_end) {
    brk += aligned_incr;
  }
  else {
    // This allocation failed, but we leave brk untouched in case a smaller allocation request follows
    // that we are able to satisfy.
    old_brk = NULL;
  }

  taskEXIT_CRITICAL();

  return old_brk;
}

// Provide our own new() and delete() so that C++ is happy.
void* operator new(size_t size)
{
  return malloc(size);
}

void operator delete(void* ptr)
{
  #if 0
  if (!ptr) {
    free(ptr);
  }
  #else
  panic("free() is not implemented!");
  #endif
}

// pvPortMalloc and vPortFree are provided by FreeRTOS-Kernel-Heap4
// Do NOT define them here - let Heap4 manage FreeRTOS object allocation