# Chunked Log Storage Design

## Problem

LittleFS performance does not fit well in applications that require storing large logfiles on SD cards, such as the umod4 project.
Some of the LittleFS design choices that lend themselves to reliability have a performance cost.
Specifically, one great aid to reliability is that LittleFS does not maintain a persistent free-space list.
Not having a free-space list means that it is impossible for the filesystem to get out of sync regarding what the free-space list thinks is in-use versus what the files in the filesystem are actually using.

The cost of not having a free-space list means that block allocation events may require a full traversal of every file's block chain to determine which blocks are in use.
Blocks that are not actively in use must be free.

This has two impacts on a system:

1) filesystems containing a large number of small files will require a lot of reads to find a free block
2) filesystems containing even a small number of very large log files will also require a lot of reads to find a free block

Given that SD cards are many tens of GB in length, it can take LittleFS literally minutes of scanning to find a free block when the SD card starts to see serious usage.

There are configuration adjustments that can be made to make the free-space searches "less bad", but fundamentally, the more data that is stored in a LittleFS filesystem, the worse LittleFS performs.
Large log files on large SD cards are a worst-case situation for LittleFS.

The delays are not necessarily predictable, which adds another wrinkle to a system using LittleFS.
Writes may be proceeding fine, completing in a small number of milliseconds, then suddenly there will be a 10 second delay to complete a write.
Under those circumstances, a system needs to be able to buffer the incoming data stream when LittleFS goes off on a free block hunt.
This can have a considerable RAM requirement.

These issues are the result of fundamental LittleFS architectural decisions, not configuration problems.
No combination of block_size, lookahead_size, or background scanning can eliminate them.
These configuration items only serve to defer the threshold before the search time overflows whatever RAM buffer can be supplied.

## Situation

My specific system falls into category 2: small numbers of very large log files.
By this, I mean less than 20 log files where each logfile might be on the order of 5 to 100 megabytes in length.

The existing logging data rate is on the order of 2K to 11K bytes/sec.
Even using a large 80K RAM buffer to absorb incoming data during a LittleFS write delay results in being able to tolerate about 7 seconds of delay before the buffer overflows.
Experiments show that for my system's specifically tuned LittleFS configuration and SD card write speeds, LittleFS can exceed this 7 second write-delay limit once my 64GB SD card has approximately 230 megabytes in its filesystem.
At that point, I am losing data even with an 80K buffer when the card's capacity is not even 0.5% used.

## Solution

LittleFS has its strengths though, and reliability is the one I am interested in.
The key is to play to its strengths while avoiding its weaknesses.

Again, the situations I am trying to deal with are:

1) Small number of giant logs (potentially > 100 megabytes)
2) log data is purely sequential, always appended
3) relatively high log data rate
4) minimize RAM buffering for situations when LittleFS is unavoidably busy

To solve all these problems, I added a thin meta filesystem on top of LittleFS called the LogStore.
This is not as complex as it might sound.

It starts by dividing the SD card's total storage space into 1024 chunks, regardless of card size.
This number can be adjusted, but 1024 chunks seems fine.
It means that there could be as many as 1023 log files of up to ~64 megabytes each when a 64GB card is being used, or a single logfile of up to 64G because a log file can use as many chunks as it can allocate.

By convention, the first chunk on the SD card gets assigned to LittleFS usage.
LittleFS creates a filesystem like normal within that chunk.
This is actually a major win: by giving LittleFS less space to manage, its worst-case searches are bounded to a much smaller block count.
The remainder of the chunks are managed by the LogStore.

The key to everything is that log data is never stored in LittleFS.
Instead, every log file created by the LogStore is defined by a small metadata structure.
This data structure explains how to find the real log data by accessing raw SD card blocks in the LogStore area.
We only depend on LittleFS to store these metadata files.

At boot time, the LogStore code must scan all log metadata files in the LittleFS system.
This is accomplished by assigning a consistent filename suffix for all metadata logfiles.
As the LogStore scans the metadata files, it makes a note of any chunk that is in use using a RAM-based freelist, much like LittleFS would do itself.
The difference is that the LogStore's RAM-resident free list data structure is tiny (128 bytes for 1024 chunks), but it describes the state of *every* chunk in the system.
A LogStore free-space search will complete in the time to scan 32 words (128 bytes) of RAM, worst case.
No SD reads required!

When a log is opened for writing, the LogStore reads the metadata file from LittleFS and puts it into a small RAM-resident data structure.
The metadata is small: the critical data is a list of the chunks it owns, and the current write offset into the logfile where new data will be appended.
To keep things simple, writes must occur in multiples of SD card sector lengths (512 bytes).

When it is time for the LogStore to write data to a log, LittleFS is used for what it does best: guarantee the reliability of the write operation:

1) a raw start sector address gets calculated based on the current chunk's start address and the current write offset
  a) if a write were go past the end of the current chunk, a new chunk would get allocated by scanning the RAM-resident free list, then adding that chunk to that log file's RAM-based metadata chunk list
2) Sectors are written sequentially from that raw sector start address until all data is written
3) Once written, the RAM-based metadata data structure is updated to increment the log length to account for the freshly written data
4) The entire RAM-based metadata structure gets written to LittleFS overwriting the original metadata file
  a) LittleFS guarantees that this re-write operation either completes or has no effect, but never corrupts the LittleFS filesystem

The advantages are clear:
1) Log data writes are extremely fast: direct SD card write operations involving sequential sectors, not going through LittleFS
  a) The low-level write operations will never trigger a LittleFS search for freespace
2) Rewriting the metadata file is efficient: metadata files are very short
  a) If LittleFS triggers a freespace search during a metadata rewrite, the number of in-use blocks to be scanned is quite small because the giant log data files are not stored in LittleFS
3) Allocating a new chunk to an existing log file will be rare (because log chunks are so huge), and fast (because the free-space structure is both tiny and RAM-resident)

The act of writing data and then updating the metadata file in LittleFS acts very much like committing a journaled write operation in a more complex filesystem.
If the "journal update" fails for any reason, you only lose the final write data: the state of the LittleFS filesystem and the log data in the log chunk area will reflect the situation just before the failed write occurred.

The tradeoffs with this approach are also clear:
1) a logfile containing 1 byte of data will allocate 60 megabytes of SD card space
2) typical write speeds will be slower than LittleFS because the metadata file must be rewritten after each write

Regarding underutilized chunks, Given that there are 1023 chunks, a 60G SD card could actually hold 1023 different files containing 1 byte, which is a lot of files. The basic number of chunks could be increased, and the only cost would be extra RAM for the freelist.
But realistically, use LittleFS to store tiny files.
LogStore is for saving giant logs.

Typical LogStore write speeds will be slower because it is writing the data and the metadata every time.
The point to remember is that typical write speeds were never the problem.
The whole LogStore solution is about avoiding the worst-case LittleFS write times when it is used for large log files on large SD cards.

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
