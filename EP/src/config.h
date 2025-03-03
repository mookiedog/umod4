#if !defined CONFIG_H
#define CONFIG_H

// The epromTask will log every single bus access the ECU makes. The accesses get stored
// in a circular buffer whose length is defined by the next symbol.
// Buffer can be any length, not just a power of two.
#define ECU_BUSLOG_LENGTH_BYTES    65536

// Consider redesigning things so that the firmware can allocate as much RAM to the
// RAM window in the EPROM space. Perhaps this firmware can define a max, and the ECU
// firmware can chose to use more or less of it.  For now, we hardcode things
// to allow the ECU to move its stack out of the HC11's internal RAM
// to an area from 0xFF00..0xFFBF. These are ECU addresses, not EPROM offsets!
#define RAM_WINDOW_START_ADDR   (0xFF00)
#define RAM_WINDOW_LEN          (0x00C0)
#define RAM_WINDOW_END_ADDR     ((RAM_WINDOW_START_ADDR)+(RAM_WINDOW_LEN)-1)


#endif
