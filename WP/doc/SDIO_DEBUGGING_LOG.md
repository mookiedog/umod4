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
