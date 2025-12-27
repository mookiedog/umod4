# SDIO Integration - Fresh Start Plan

**Date:** 2025-12-27
**Branch:** `feature/sdio-simple`
**Goal:** Integrate SDIO_RP2350 library for 4-bit SD card access (~20-25 MB/s vs ~3 MB/s SPI)

---

## Current Status

### What We Have
- SDIO_RP2350 library copied to `WP/src/sdio/` (MIT licensed)
- Configuration file: `WP/src/sdio/sdio_rp2350_config.h` (working, uses pio2, DMA 4/5, 20 MHz)
- SdCardSDIO.cpp/h exist but have accumulated debugging changes that may have introduced problems
- **Critical finding:** testCard() successfully reads 96+ blocks, proving SDIO hardware works perfectly

### The Problem
After extensive debugging, we've accumulated too many "fixes" that may have introduced new issues:
- CMD13 status checks before every read
- 1ms delays after transfers
- SDIO_FLAG_STOP_CLK flags
- Aggressive reinit on errors
- Multiple retry loops
- taskYIELD() calls in poll loops
- Excessive printf() debug logging

**User observation:** "We have accumulated a bunch of junk changes that may have introduced unnecessary problems that we are now chasing."

---

## Critical Lessons Learned

### 1. The SDIO Library Works Perfectly
- testCard() reads 96+ consecutive blocks successfully
- The problem is NOT with SDIO hardware or timing
- Speed (20 MHz SDIO_MMC) is proven stable - don't reduce it

### 2. Use the Library's Own Pattern
**Reference:** `/home/robin/projects/SDIO_RP2350/src/sdfat_sdcard_rp2350.cpp`

The library's own SdFat integration shows the correct approach:
```cpp
// Simple poll loop (lines 1163-1171)
do {
    uint32_t blocks_done;
    g_sdio_error = rp2350_sdio_rx_poll(&blocks_done);

    if (callback) {
        callback(g_sd_callback.bytes_start + SDIO_BLOCK_SIZE * blocks_done);
    }
} while (g_sdio_error == SDIO_BUSY);
```

**Key observations:**
- ✅ Tight busy-wait loop (no taskYIELD needed if using callbacks)
- ✅ Optional callback for cooperative multitasking
- ✅ No CMD13 status checks before reads
- ✅ No extra delays after transfers
- ✅ Simple retry: if multi-block fails, fall back to single-block

### 3. Don't Add "Safety" Code Preemptively
Things we added that were likely unnecessary:
- CMD13 checks (library doesn't do this)
- Delays after successful reads (library doesn't do this)
- SDIO_FLAG_STOP_CLK (library doesn't use this)
- Aggressive SDIO reinit on errors (library just retries)

### 4. Printf from Interrupt Context is Dangerous
- SDIO_ERRMSG/CRITMSG call printf() from DMA interrupt
- This can cause hangs and prevent debug output
- Consider disabling these macros in config

### 5. LittleFS Already Protects Filesystem Operations
- LittleFS has `lfs_mutex` that serializes all operations
- Don't add additional mutexes

---

## The Clean Integration Plan

### Step 1: Backup Current Work
```bash
cd /home/robin/projects/umod4
git add WP/src/SdCardSDIO.cpp WP/src/SdCardSDIO.h
git commit -m "WIP: SDIO integration before fresh start"
```

### Step 2: Create Minimal SdCardSDIO Based on Library Pattern

**Copy the working pattern from** `sdfat_sdcard_rp2350.cpp`:

#### Init Sequence (keep what we have - it works)
- CMD0, CMD8, ACMD41, CMD2, CMD3, CMD9, CMD7, ACMD6, CMD16
- Use SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG for R2/R3 responses
- Initialize at 300 kHz, then switch to 20 MHz (SDIO_MMC)
- Re-enable pullups after each `rp2350_sdio_init()`

#### Read Function (minimal approach)
```cpp
SdErr_t SdCardSDIO::read(lfs_block_t block_num, lfs_off_t off,
                          void *buffer, lfs_size_t size) {
    if (!operational()) return SD_ERR_NOT_OPERATIONAL;
    if ((size & 0x1FF) != 0 || ((uintptr_t)buffer & 3) != 0) {
        return SD_ERR_BAD_ARG;
    }

    uint32_t num_blocks = size / 512;

    // Single command - no retries initially
    uint8_t cmd = (num_blocks == 1) ? CMD17 : CMD18;
    uint32_t reply;
    if (rp2350_sdio_command_u32(cmd, block_num, &reply, 0) != SDIO_OK) {
        return SD_ERR_IO;
    }

    // Start DMA
    if (rp2350_sdio_rx_start((uint8_t*)buffer, num_blocks, 512) != SDIO_OK) {
        rp2350_sdio_stop();
        return SD_ERR_IO;
    }

    // Simple poll loop (like library example)
    sdio_status_t status;
    do {
        status = rp2350_sdio_rx_poll(nullptr);
    } while (status == SDIO_BUSY);

    // Stop and return
    rp2350_sdio_stop();
    return (status == SDIO_OK) ? SD_ERR_NOERR : SD_ERR_DATA_ERROR;
}
```

**Note:** Start with tight busy-wait. If FreeRTOS cooperation is needed, add callback mechanism (not taskYIELD).

#### Write Function (same minimal pattern)
```cpp
SdErr_t SdCardSDIO::prog(lfs_block_t block_num, lfs_off_t off,
                          const void *buffer, lfs_size_t size_bytes) {
    if (!operational()) return SD_ERR_NOT_OPERATIONAL;
    if ((size_bytes & 0x1FF) != 0 || ((uintptr_t)buffer & 3) != 0) {
        return SD_ERR_BAD_ARG;
    }

    uint32_t num_blocks = size_bytes / 512;
    uint32_t reply;

    // Issue write command
    uint8_t cmd = (num_blocks == 1) ? CMD24 : CMD25;
    if (rp2350_sdio_command_u32(cmd, block_num, &reply, 0) != SDIO_OK) {
        return SD_ERR_IO;
    }

    // Start DMA
    if (rp2350_sdio_tx_start((const uint8_t*)buffer, num_blocks, 512) != SDIO_OK) {
        rp2350_sdio_stop();
        return SD_ERR_WRITE_FAILURE;
    }

    // Simple poll loop
    sdio_status_t status;
    do {
        status = rp2350_sdio_tx_poll(nullptr);
    } while (status == SDIO_BUSY);

    // Stop and return
    rp2350_sdio_stop();
    return (status == SDIO_OK) ? SD_ERR_NOERR : SD_ERR_WRITE_FAILURE;
}
```

#### testCard() - Keep Simple
```cpp
SdErr_t SdCardSDIO::testCard() {
    uint8_t buffer[512] __attribute__((aligned(4)));

    printf("Testing SDIO card read access...\n");

    // Test first block
    if (read(0, 0, buffer, sizeof(buffer)) != SD_ERR_NOERR) {
        return SD_ERR_IO;
    }

    // Test last block
    uint32_t lastBlock = getCardCapacity_blocks() - 1;
    if (read(lastBlock, 0, buffer, sizeof(buffer)) != SD_ERR_NOERR) {
        return SD_ERR_IO;
    }

    printf("SDIO card read access test passed.\n");
    return SD_ERR_NOERR;
}
```

### Step 3: Minimal Debug Output
- **Disable** SDIO_ERRMSG and SDIO_CRITMSG in config (they call printf from IRQ)
- **Remove** all printf from read/write functions initially
- **Only** add targeted debug when specific issues arise

### Step 4: Test Incrementally
1. Boot and verify init succeeds
2. Run testCard() - should pass (it did before)
3. Mount LittleFS without format
4. If mount succeeds - DONE, it works!
5. If mount fails, add minimal logging to understand why

---

## Files to Create/Modify

### New Files
- `WP/src/SdCardSDIO.cpp` - Clean implementation following library pattern
- `WP/src/SdCardSDIO.h` - Minimal header (keep existing structure)

### Existing Files to Keep
- `WP/src/sdio/sdio_rp2350_config.h` - **Working config, don't touch**
- `WP/src/sdio/sdio_rp2350.cpp` - Library code, don't modify
- `WP/src/sdio/sdio_rp2350.h` - Library header, don't modify
- `WP/src/sdio/sdio_rp2350.pio` - PIO programs, don't modify
- `WP/CMakeLists.txt` - Already configured for SDIO build

### Config Changes (Optional)
In `sdio_rp2350_config.h`, consider:
```c
// Disable printf from interrupt context
// #define SDIO_ERRMSG(txt, arg1, arg2) printf(txt " %lu %lu\n", (uint32_t)(arg1), (uint32_t)(arg2))
// #define SDIO_CRITMSG(txt, arg1, arg2) printf(txt " %lu %lu\n", (uint32_t)(arg1), (uint32_t)(arg2))
#define SDIO_ERRMSG(txt, arg1, arg2)   // Disabled
#define SDIO_CRITMSG(txt, arg1, arg2)  // Disabled
```

---

## What NOT to Do

❌ Don't add CMD13 status checks before every operation
❌ Don't add delays after successful transfers
❌ Don't use SDIO_FLAG_STOP_CLK
❌ Don't add aggressive SDIO reinit on first error
❌ Don't add taskYIELD() without understanding callback mechanism
❌ Don't add printf() to every read/write
❌ Don't reduce speed from 20 MHz (proven stable)
❌ Don't add extra "safety" code preemptively

---

## If Problems Arise

### Test Procedure
1. Does init() succeed? → Check CMD sequence
2. Does testCard() pass? → SDIO hardware is fine
3. Does LittleFS mount fail? → Likely write issue, not read
4. Add **one** targeted printf at a time to narrow down issue

### Debug Strategy
- **First:** Verify writes are actually being called
- **Second:** Check if writes return success
- **Third:** Read back written blocks to verify data
- **Only then:** Add more complex diagnostics

---

## Success Criteria

✅ Card initializes at 20 MHz
✅ testCard() reads blocks successfully
✅ LittleFS mounts existing filesystem
✅ LittleFS can create new files
✅ System remains stable during continuous operation

---

## Reference Files

**Library example:** `/home/robin/projects/SDIO_RP2350/src/sdfat_sdcard_rp2350.cpp`
**Working config:** `/home/robin/projects/umod4/WP/src/sdio/sdio_rp2350_config.h`
**SPI version (for comparison):** `/home/robin/projects/umod4/WP/src/SdCard.cpp`

---

## Next Steps (for fresh session)

1. Read this document
2. Create minimal SdCardSDIO.cpp following the patterns above
3. Build and test incrementally
4. **Resist the urge to add "fixes" before proving they're needed**
5. Trust that the library works (testCard proved it does)
