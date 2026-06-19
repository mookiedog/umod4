# Chunked Log Storage Design

## Problem

LittleFS does not maintain a persistent free-space index. Every block allocation
requires a full traversal of every file's block chain to determine which blocks
are in use. On a large SD card with accumulated log data, this scan takes
O(total_data / block_size) sector reads at ~500 us each.

At 650 MB of accumulated log files the scan takes ~20 seconds per pass through
a single 8192-block lookahead window. On a 32 GB card that can require up to
244 passes -- over 80 minutes -- to find a free block.

The Logger's 80 KB ring buffer overflows in ~7 seconds at redline (11 KB/s).
The system becomes unusable once accumulated file data exceeds roughly 230 MB
(redline) to 356 MB (cruising at 7 KB/s).

This is a fundamental LittleFS architectural limitation, not a configuration
problem. No combination of block_size, lookahead_size, or background scanning
can eliminate it -- only defer the threshold.

## Design: Fixed-Count Chunked Raw Storage

Split the SD card into a fixed number of equal-sized chunks (1024). Chunk size
adapts to the card: 64 GB card = 60 MiB chunks, 128 GB = 120 MiB, etc. Use
LittleFS only for small metadata and config files. Write log data directly to
raw SD sectors, bypassing LittleFS entirely for all bulk I/O.

### Card Layout

```
 Chunk 0          Chunk 1          Chunk 2              Chunk 1023
+------------+   +------------+   +------------+       +------------+
|  LittleFS  |   |  Raw log   |   |  Raw log   |  ...  |  Raw log   |
|  (config)  |   |   data     |   |   data     |       |   data     |
+------------+   +------------+   +------------+       +------------+
```

- **Chunk 0**: LittleFS partition for config files (background.jpg,
  ecu_live.json, log metadata files). Total data always under 100 KB.
  LFS allocator scans are instant.
- **Chunks 1-1023**: raw storage for log data. Each chunk is written
  sequentially -- no filesystem, no allocation, no fragmentation.

### Why 1024 Fixed Chunks

Rather than fixing the chunk size and letting the count vary with card size,
the chunk count is fixed at 1024. Chunk size adapts automatically:

- 64 GB card: ~60 MiB per chunk
- 128 GB card: ~120 MiB per chunk
- 2 TB card: ~2 GiB per chunk

This means:
- The free-chunk bitmap is always exactly 128 bytes regardless of card size.
- No MAX_CHUNKS constant or what-if calculations.
- Works on any card from 16 GB to 2 TB with zero code changes.
- Chunk-to-sector mapping: `chunk_start = chunk_number * (total_sectors / 1024)`

### Scaling by Card Size

| Card | Chunk size | Free bitmap | Short rides (~5 MB) | Long rides (~100 MB) |
|---|---|---|---|---|
| 16 GB | ~15 MiB | 128 bytes | ~1000 | ~150 |
| 32 GB | ~30 MiB | 128 bytes | ~1000 | ~300 |
| 64 GB | ~60 MiB | 128 bytes | ~1000 | ~600 |
| 128 GB | ~120 MiB | 128 bytes | ~1000 | ~1000 |

A typical rider could have 1023 rides before running out of room.

## Operation

### Boot Sequence

1. Mount LittleFS on chunk 0.
2. Read all log metadata files from LFS (strictly `log_DIGITS.meta` --
   test files like `log_t900.meta` are ignored to prevent log number
   pollution if left behind after a crash).
3. Build a free-chunk bitmap in RAM (128 bytes) by marking every chunk
   referenced by any log as "in use".
4. Run integrity check (FSCK) to detect corruption.
5. Close any previously-active log (unclean shutdown recovery).

Cost: a handful of small LFS file reads -- milliseconds.

### Creating a New Log

1. Scan the free-chunk bitmap for the first zero bit. Cost: nanoseconds.
2. Mark the chunk as in-use in the RAM bitmap.
3. Create a new LFS metadata file (e.g. /log_42.meta) recording the
   chunk number and initial write offset of 0.

### Writing Log Data

Two concerns are deliberately separated: **data durability** (how much is
lost on power-off) and **metadata consistency** (which chunks belong to
which log). Only the first one is time-critical.

**Data path (every flush):**

1. Logger accumulates data in its ring buffer until the flush threshold
   is reached (4 KB default, must be a multiple of 512 bytes).
2. Write the batch to the next sequential sectors in the current raw
   chunk -- just writeSectors(), no filesystem overhead.
3. Update the LFS metadata file with the new write offset and sync.
   LFS's copy-on-write journaling guarantees that either the old or
   new offset survives a power loss -- never a corrupted half-write.

All SD card access (both LFS and raw LogStore I/O) is serialized
through a shared mutex (`sd_mutex`) to prevent bus collisions.

**Data loss on power-off:**

Logs never get closed gracefully -- the ignition key is turned off with
no advance warning. Data already written to SD sectors is safe. The LFS
metadata records the last synced write offset. The only loss is:

- Data written to raw sectors since the last LFS metadata sync (if the
  sync and raw write are done together, this is zero).
- Data still in the Logger's ring buffer at the moment of power loss.

The flush threshold controls total data loss:

| Flush size | Loss at 11 KB/s | Loss at 7 KB/s | SD writes/sec | Note |
|---|---|---|---|---|
| 16 KB | 1.5 s | 2.3 s | 0.7 | Previous Logger block size |
| 4 KB | 0.36 s | 0.57 s | 2.8 | Current default |
| 512 B | 0.05 s | 0.07 s | 21.5 | Practically lossless |

Each flush is one raw data write plus one small LFS metadata sync.
LFS operations are fast because the LFS partition contains under
100 KB of data total -- allocator scans are sub-millisecond.

**Metadata path (chunk transitions only):**

The LFS metadata file also records the chunk list for each log. This
only changes when a new chunk is allocated (every ~2.5 hours at
redline on a 64 GB card), so it adds negligible load.

### Chunk Boundary Crossing

When the Logger fills the current chunk:

1. Allocate the next free chunk (bitmap scan -- nanoseconds).
2. Append the new chunk number to the log's LFS metadata file and sync.
3. Continue writing sequentially in the new chunk.

Chunk crossing is inexpensive -- roughly double the cost of a normal
write (one extra metadata update).

### Ending a Log (Power-Off)

Logs are never closed gracefully -- the ignition key is turned off
without warning. The last LFS metadata sync is the final record of
how much data was captured. On the next boot, the system reads the
metadata to determine the log's true length and marks it as closed
before creating a new log.

### Deleting a Log

1. Delete the LFS metadata file for the log.
2. The freed chunks are automatically reclaimed at next boot when the
   bitmap is reconstructed from the remaining metadata files.
   (Or update the in-memory bitmap immediately.)

### Reading a Log (HTTP Download)

1. Read the log's LFS metadata to get the ordered chunk list and total
   length.
2. Read raw sectors sequentially from each chunk in order.
3. The existing chunked HTTP transfer protocol maps naturally --
   each transfer chunk is a sequential sector read.

The log decoder (decodelog.py) sees a contiguous byte stream regardless
of on-device storage layout. Log files are presented to clients as
`log_N.um4` -- the `.meta` internal naming is invisible to the API.

## LFS Metadata File Format

Each active or closed log has a small metadata file in LFS.
Filename: `/log_42.meta` (presented externally as `log_42.um4`).

Contents (JSON):

```json
{"log":42,"active":true,"chunks":[5,6],"offset":34816000,"total":97183744}
```

For a single-chunk log (the common case), the chunks list has one entry.
The file is always under 200 bytes.

## Integrity Check (FSCK)

A filesystem integrity check runs at every boot and is available on
demand via the `logstore_fsck` VFY command. It scans ALL `.meta` files
(including test files) and checks for:

- **CORRUPT**: metadata file that cannot be parsed
- **CHUNK0**: log referencing chunk 0 (reserved for LFS)
- **RANGE**: chunk number >= 1024 (out of range)
- **DOUBLE**: two logs claiming the same chunk
- **OFFSET**: write_offset exceeding the chunk size
- **SIZE**: total_bytes inconsistent with chunk count + write_offset

Errors are reported to both printf (RTT ch0) and vfy_printf (RTT ch1)
with a keyword prefix for machine parsing.

Test metadata files use `log_tNNN.meta` naming (note the `t` prefix).
Boot-time init strictly matches `log_DIGITS.meta` and ignores test
files, so they are harmless if left behind after a crash.

## What Stays in LittleFS

LittleFS continues to manage:

- background.jpg (~26 KB) -- web UI asset
- ecu_live.json (~44 bytes) -- live stream configuration
- Log metadata files (~100 bytes each)
- Any future small config files

Total LFS data: always under 100 KB regardless of how many logs exist.
The LFS allocator scan traverses this data in under 1 millisecond.

## Why Not FAT?

FAT has O(1) block allocation (persistent allocation table), which would
solve the LFS scaling problem. But FAT metadata is not power-fail safe.
A write interrupted by ignition-off can leave the FAT table or directory
entries in an inconsistent state, requiring fsck -- which is unacceptable
in a headless embedded system on a motorcycle.

The only way to make FAT reliable in this environment would be battery
backup to guarantee that in-progress writes complete after ignition-off.
That is a major PCB design change (new rev, battery management circuit,
charge controller, hold-up time validation) that this design avoids
entirely.

LittleFS + raw log chunks gives us the best of both: LFS's copy-on-write
crash safety for metadata, and O(1) sequential writes for bulk data --
no battery backup required, no fsck, no PCB changes.

## What This Eliminates

- LFS block allocator scans during logging: **gone**
- LFS performance degradation with accumulated data: **gone**
- Need for periodic filesystem cleanup to prevent hangs: **gone**
  (cleanup is still good practice for managing card capacity, but
  the system no longer hangs if cleanup is skipped)
- Risk of multi-minute boot delays: **gone**
- **80 KB Logger ring buffer**: the current 80 KB ring buffer exists
  solely to absorb LFS write latencies that can reach 20+ seconds.
  With raw sector writes completing in under 1 ms, a much smaller
  buffer suffices. This frees significant RAM on a device where
  heap headroom is currently 3-14 KB.

## Implementation Considerations

- **SD card abstraction**: the existing SdCardSDIO::readSectors() /
  writeSectors() interface works directly for raw chunk I/O.
- **Chunk-to-sector mapping**: chunk N starts at sector
  N * (total_sectors / 1024). Computed at init from actual card size.
- **LFS block_count**: set to chunk_0_sectors / LFS_BLOCK_SIZE so LFS
  only sees chunk 0. The rest of the card is invisible to LFS.
- **SD card bus locking**: a shared mutex (`sd_mutex`) serializes all
  SD card access -- both LFS callbacks and LogStore raw sector I/O.
- **HTTP file serving**: fs_custom.cpp routes `log_N.um4` downloads
  through LogStore::readLog() for raw chunk reads; other files use LFS.
- **Shell commands**: ls shows LogStore logs alongside LFS files
  (`.meta` files are hidden). rm routes `log_N.um4` to LogStore.
- **Backward compatibility**: first boot after this change reformats
  LFS with the smaller block_count. Config files (background.jpg,
  ecu_live.json) must be re-uploaded. Handled by provisioning.
- **OTA uploads**: .uf2 files (up to ~3 MB) go into LFS as normal.
  The OTA task should delete the .uf2 file after a successful flash
  to avoid leaving dead weight in the LFS partition.
