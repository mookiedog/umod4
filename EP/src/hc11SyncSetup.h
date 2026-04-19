#if !defined HC11SYNCSETUP_H
#define HC11SYNCSETUP_H

// Initializes PIO1 SM0 as the HC11 E-clock synchronizer and Core 1 wakeup source.
// Must be called from Core 0 after busFailsafeSetup() and before startCore1().
void hc11SyncSetup();

#endif
