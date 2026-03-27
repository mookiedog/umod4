#if !defined CONFIG_H
#define CONFIG_H

// The epromTask will log every single bus access the ECU makes. The accesses get stored
// in a circular buffer whose length is defined by the next symbol.
// Buffer can be any length, not just a power of two.
#define ECU_BUSLOG_LENGTH_BYTES    32768

// Consider redesigning things so that the RAM window definition lives inside the
// ECU binary itself (e.g. in the metadata block), so the EP can read it at load
// time and support different UM4 versions with different RAM window layouts.
// For now it is hardcoded here and must match fakeRamStart/STACK_TOP in UM4.S.
// These are ECU addresses, not EPROM offsets!
#define RAM_WINDOW_START_ADDR   (0xFF00)
#define RAM_WINDOW_LEN          (0x00C0)
#define RAM_WINDOW_END_ADDR     ((RAM_WINDOW_START_ADDR)+(RAM_WINDOW_LEN)-1)


#endif
