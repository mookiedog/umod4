// The following defines a simplified memory map for RP58-compatible builds.
// The values int the table below represent actual ECU memory address, not EPROM offsets.
//
//    $8000..$9BFF ( 7168 bytes)  Monolithic map data blob, rounded up past its actual end at $9BD8
//    $9C00..$9FDF (  992 bytes)  Unused, set to $3F
//    $9FE0..$9FFF (   32 bytes)  diag code table, rounded from 27 bytes up to 32
//    $A000..$BFFF ( 8192 bytes)  Unused, set to $3F
//    $C000..$FFFF (16384 bytes)  Code space. First instruction is actually at $C003.
//                                Note that $C000..$C002 will be 'UM4' for UM4 builds, $3F for all others.

// The offsets below represent EPROM offsets, not ECU addresses:
#define RP58_MAPBLOB_OFFSET         0x0000
#define RP58_MAPBLOB_LENGTH         0x1C00

#define RP58_UNUSED_1_OFFSET        0x1C00
#define RP58_UNUSED_1_LENGTH        0x03E0

#define RP58_DIAGCODE_TBL_OFFSET    0x1FE0
#define RP58_DIAGCODE_TBL_LENGTH    0x0020

#define RP58_UNUSED_2_OFFSET        0x2000
#define RP58_UNUSED_2_LENGTH        0x2000

#define RP58_CODEBLOB_OFFSET        0x4000
#define RP58_CODEBLOB_LENGTH        0x4000

// For the purposes of determining if some random .bin file is UM4/RP58-compatible,
// perform a murmur3 hash of the eprom from ECU address $4003..$FFFF.
// A result of 0x4CF503CB means that the codebase is compatible.

#define RP58_CODEHASH_OFFSET        0x4003
#define RP58_CODEHASH_LENGTH        (0x8000-(RP58_CODEHASH_OFFSET))
#define RP58_CODEHASH_M3            0x4CF503CB
