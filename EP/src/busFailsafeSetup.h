#if !defined BUSFAILSAFESETUP_H
#define BUSFAILSAFESETUP_H

// Initializes PIO0 SM1 as the HC11 data bus output driver and dead-man failsafe.
// Must be called from Core 0 before startCore1().
void busFailsafeSetup();

// Address of PIO0 SM1 TX FIFO — Core 1 writes the read-cycle data byte here.
// Set by busFailsafeSetup(); valid after that call returns.
extern "C" volatile uint32_t g_failsafe_txf_addr;

#endif
