# SDIO Debugging Log

This document tracks recurring issues and their solutions during SDIO integration.

## Issue 1: CMD0 Timeout - "Timeout waiting for command response"

**Symptoms:**
```
init: Sending CMD0 (reset)
SDIO Command start 0 0
Timeout waiting for command response 0 2
Command complete 0 2158362912
init: CMD0 failed, err=-4
```

**Root Cause:** CMD0 doesn't expect a response, but code was using `rp2350_sdio_command_u32()` which waits for a response.

**Solution:** Use `rp2350_sdio_command(CMD0, 0, nullptr, 0, 0)` with `resp_bytes = 0` to indicate no response expected.

**File:** `WP/src/SdCardSDIO.cpp` line 118

---

## Issue 2: CMD8 Timeout - SAME SYMPTOM, DIFFERENT ROOT CAUSE

**Symptoms:**
```
init: Sending CMD8 (check voltage)
SDIO Command start 8 426
Timeout waiting for command response 8 2
Command complete 8 587202560
```

**Important Notes:**
- "Command complete" message shows the command DOES eventually complete
- The response value (587202560 = 0x23010100) is valid
- This means the timeout is too short, NOT that the command is failing

**Root Cause:** The printf debug messages from DMA interrupt context are causing timing delays that make commands appear to timeout, even though they complete successfully.

**Solution (from previous session):** DISABLE the SDIO debug messages:
```c
// In sdio_rp2350_config.h, change from:
#define SDIO_DBGMSG(txt, arg1, arg2) printf(txt " %lu %lu\n", ...)
#define SDIO_ERRMSG(txt, arg1, arg2) printf(txt " %lu %lu\n", ...)
#define SDIO_CRITMSG(txt, arg1, arg2) printf(txt " %lu %lu\n", ...)

// To:
#define SDIO_DBGMSG(txt, arg1, arg2)
#define SDIO_ERRMSG(txt, arg1, arg2)
#define SDIO_CRITMSG(txt, arg1, arg2)
```

**Why This Happens:**
- SDIO library calls printf from DMA interrupt context
- Printf is slow and blocks
- This causes the DMA completion to be delayed
- The timeout check happens BEFORE printf completes
- The command completes AFTER the timeout is reported
- This is a FALSE TIMEOUT - the command works fine without debug enabled

**File:** `WP/src/sdio/sdio_rp2350_config.h` lines 11-13

**DO NOT:** Increase SDIO_CMD_TIMEOUT_US - this doesn't fix the real problem and just masks it

---

## Issue 3: CMD8 Fails with SD_ERR_BAD_CARD (-2) - CARD IN SPI MODE!

**Symptoms:**
```
init: Sending CMD0 (reset)
init: Sending CMD8 (check voltage)
init: CMD8 failed, err=-2
```
Commands verified correct on scope, but card doesn't respond.

**Root Cause:** DAT3 pin (GPIO 15, same as SD_CS_PIN) was floating or low during card power-up, causing card to enter SPI mode instead of SDIO mode. Once in SPI mode, card ignores SDIO commands.

**Critical Insight:** SD cards select their interface mode (SPI vs SDIO) based on CS/DAT3 pin state during power-up:
- DAT3 HIGH (pullup) → SDIO mode
- DAT3 LOW (driven or floating) → SPI mode

**Solution:** Initialize all SDIO GPIO pins with pullups BEFORE the 30ms power-up delay, so DAT3 is high when card powers up.

**Code Change in `WP/src/SdCardSDIO.cpp` init():**
```cpp
// BEFORE power-up delay:
gpio_init(SDIO_CLK);
gpio_init(SDIO_CMD);
gpio_init(SDIO_D0);
gpio_init(SDIO_D1);
gpio_init(SDIO_D2);
gpio_init(SDIO_D3);  // DAT3 = CS, MUST be high!
gpio_set_dir(all, GPIO_IN);
gpio_pull_up(all);

// THEN wait for power-up
vTaskDelay(pdMS_TO_TICKS(30));
```

**File:** `WP/src/SdCardSDIO.cpp` lines 239-263

---

## Issue 4: ACMD41 Fails with SD_ERR_NO_INIT (-4)

**Symptoms:**
```
init: Sending CMD0 (reset)
init: Sending CMD8 (check voltage)
init: Sending CMD55+ACMD41 (initialize)
init: ACMD41 failed, err=-4
```

**Root Cause:** ACMD41 command was missing the `SDIO_FLAG_NO_CMD_TAG` flag, causing the SDIO library to reject the command.

**Solution:** Add `SDIO_FLAG_NO_CMD_TAG` to ACMD41 flags, matching the library reference implementation.

**Code Change in `WP/src/SdCardSDIO.cpp` initializeCard():**
```cpp
// ACMD41 - Start initialization, indicate HC support
// Must use SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG per library example
if (rp2350_sdio_command_u32(ACMD41, 0x40300000, &reply, SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG) != SDIO_OK) {
  return SD_ERR_NO_INIT;
}
```

**File:** `WP/src/SdCardSDIO.cpp` lines 159-200

**Reference:** `/home/robin/projects/SDIO_RP2350/src/sdfat_sdcard_rp2350.cpp` line 311

---

## Issue 5: CMD9 (Read CSD) Returns SD_ERR_CSD_VERSION (-20)

**Symptoms:**
```
init: Sending CMD9 (read CSD)
init: CMD9 failed, err=-20
```

**Root Cause:** Incorrect byte order conversion when parsing the 128-bit R2 response from CMD9. Code was manually reordering bytes, but the SDIO library already returns the response in the correct byte order for direct use.

**Solution:** Use `memcpy()` to copy the R2 response directly instead of manually reordering bytes.

**Code Change in `WP/src/SdCardSDIO.cpp` readCSD():**
```cpp
// Before (incorrect):
for (int i = 0; i < 4; i++) {
  uint32_t word = reply[3 - i];
  regCSD[i * 4 + 0] = (word >> 24) & 0xFF;
  regCSD[i * 4 + 1] = (word >> 16) & 0xFF;
  regCSD[i * 4 + 2] = (word >> 8) & 0xFF;
  regCSD[i * 4 + 3] = (word >> 0) & 0xFF;
}

// After (correct):
memcpy(regCSD, reply, 16);
```

**File:** `WP/src/SdCardSDIO.cpp` line 138

---

## Issue 6: Write Operations Fail with LFS_ERR_IO (-5)

**Symptoms:**
```
init: SDIO init complete!
Testing SDIO card read access...
SDIO card read access test passed.
comingOnline: Filesystem mounted in 2.18 milliseconds
logTask: Write 512 bytes failed: -5 bytes written
```

**Root Cause:** Incorrect command selection for read/write operations. Code was using CMD18/CMD25 (multi-block commands) for all transfers, but the SDIO library expects CMD17/CMD24 for single-block operations and CMD18/CMD25 only for multi-block transfers.

**Solution:** Use CMD17 for single-block reads, CMD18 for multi-block reads, CMD24 for single-block writes, CMD25 for multi-block writes. All commands use `SDIO_FLAG_STOP_CLK` flag.

**Code Change in `WP/src/SdCardSDIO.cpp` read() and prog():**
```cpp
// Read function:
uint8_t cmd = (num_blocks == 1) ? CMD17 : CMD18;
if (rp2350_sdio_command_u32(cmd, addr, &reply, SDIO_FLAG_STOP_CLK) != SDIO_OK) {
  rp2350_sdio_stop();
  return SD_ERR_IO;
}

// Write function:
uint8_t cmd = (num_blocks == 1) ? CMD24 : CMD25;
if (rp2350_sdio_command_u32(cmd, addr, &reply, SDIO_FLAG_STOP_CLK) != SDIO_OK) {
  rp2350_sdio_stop();
  return SD_ERR_IO;
}
```

**Reference:** `/home/robin/projects/SDIO_RP2350/src/sdfat_sdcard_rp2350.cpp` functions `read_single_sector` (line 134) and `write_single_sector` (line 182)

**File:** `WP/src/SdCardSDIO.cpp` lines 393-477

---

## Issue 7: CMD13 "Blasting" False Timeouts after ~1 Hour with Browser Open

**Symptoms:**
```
Timeout waiting for command response 13 2097154
Timeout waiting for command response 13 2097154
Timeout waiting for command response 13 2097154
... (thousands of times per second)
```
System crashes after roughly 1 hour when a browser tab with ecu_live.html (or any
polling page) is kept open. Debugger reset fixes it - SD card is fine.

**Diagnostic Data Decoded:** `2097154 = 0x200002`:

- Upper 16 bits (SDIO_PIO->flevel) = `0x0020` → RX FIFO for SM0 has **2 words waiting**
- Lower 16 bits (DMA transfer_count) = `0x0002` → DMA has **2 transfers remaining**

This means the CMD13 response had **already arrived** in the FIFO. The DMA had just
received the DREQ signal and was mid-transfer when the timeout fired.

### Root Cause: CMD13 Response Arrives Near the Timeout Boundary

The SDIO command function (`rp2350_sdio_command`) uses a busy-wait DMA poll loop:

```c
uint32_t start = SDIO_TIME_US();
while (dma_channel_is_busy(SDIO_DMACH_A)) {
    if (SDIO_ELAPSED_US(start) > timeout) {  // timeout = ~53us
        if (!dma_channel_is_busy(SDIO_DMACH_A))  // line 351: race check
            break;
        SDIO_ERRMSG(...);
        dma_channel_abort(SDIO_DMACH_A);
        ...
        return SDIO_ERR_RESPONSE_TIMEOUT;    // FALSE TIMEOUT!
    }
}
```

CMD13 normally completes in ~3μs (48-bit command + NCR + 48-bit response at 50MHz).
The 50μs timeout is ~16× the expected time. Occasionally, the card's response arrives
much later than usual — near or at the 50μs boundary. The DMA DREQ fires at this
instant, placing the response words in the FIFO and initiating a transfer. The timeout
check happens at this exact moment: DMA is still busy (transfer just started), the
FIFO has 2 words, and line 351's re-check also sees DMA busy → timeout is declared
and SDIO state is corrupted.

**Why task preemption is NOT the mechanism:** DMA is autonomous hardware. If the
Logger task is preempted for milliseconds (e.g. by the CYW43 WiFi task), the card
responds within ~3μs and DMA completes independently during that time. When the task
resumes, `dma_channel_is_busy()` returns false immediately — the outer `while` loop
exits without ever entering the timeout branch. Preemption cannot cause this failure.

**Why it appeared with browser polling:** The live-log page polls every 2 seconds,
keeping WiFi active with steady traffic. This increases both write frequency (more
data to log) and potentially introduces power supply or EMI effects from CYW43 RF
activity that can affect SD card response timing. Over ~1 hour, the rare event of a
~50μs response becomes probable.

**Secondary Issue: Logger tight loop amplifies the failure.**
After the first timeout, Logger enters WRITE_FAILURE → OPEN_LOG → UNMOUNTED
with no delay (when lfs != null), calling CMD13 thousands of times per second.
Each attempt also times out because the SDIO state machine is now corrupted.

**Note on recovery code bug in library (do not fix):**
The timeout recovery in sdio_rp2350.cpp uses:

```c
pio_sm_exec(SDIO_PIO, SDIO_SM, pio_encode_jmp(g_sdio.pio_offset.sdio_cmd));
// MISSING: | pio_encode_sideset(1, 1)  → CLK goes LOW briefly (violates SD spec)
```

CLK briefly goes LOW on timeout recovery. This is a bug in the external library.
Since we fix the root cause (timeout), this code path is not normally reached.

### Solution

1. **Increase SDIO_CMD_TIMEOUT_US** from 50 to 5000 in `sdio_rp2350_config.h`:

```c
// Before: #define SDIO_CMD_TIMEOUT_US 50
#define SDIO_CMD_TIMEOUT_US 5000  // SD cards can occasionally respond slowly; 5ms provides safe margin
```

A 5ms window makes timeouts at the boundary essentially impossible while still
detecting genuine failures promptly. CMD13 normally completes in ~3μs.

2. **Add delay after WRITE_FAILURE** in `Logger.cpp` to prevent blasting:

```c
case WRITE_FAILURE:
    lfs_file_close(lfs, &logf);
    vTaskDelay(pdMS_TO_TICKS(1000));  // Don't hammer SDIO on repeated errors
    state = OPEN_LOG;
    break;
```

**Note on Issue 2 advice "DO NOT increase SDIO_CMD_TIMEOUT_US":**
That advice applied specifically to printf-from-DMA-ISR delays (where the real fix
was to disable SDIO_DBGMSG). Here the cause is slow card responses near the timeout
boundary, which cannot be fixed by disabling debug prints. Increasing the timeout IS
the correct fix.

---

## Lesson Learned

From `SDIO_FRESH_START.md`:
> **CRITICAL WARNING:** SDIO_ERRMSG and SDIO_CRITMSG call printf from DMA interrupt context.
> This is EXTREMELY DANGEROUS and can cause hangs or false timeouts.
> Only enable for initial debugging to find where init hangs, then IMMEDIATELY disable.

**Debugging Process:**
1. Enable debug messages to see where init sequence hangs
2. Fix the actual code bug (like CMD0 needing no response)
3. IMMEDIATELY disable debug messages once you identify the problem
4. Test with debug disabled to verify it works properly
