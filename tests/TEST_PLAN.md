# umod4 Test Plan

## Philosophy

Tests are divided into two categories:

**Non-destructive** — safe to run any time. Read-only or use scratch areas that
don't affect system state. These form the baseline "is the board healthy?" check.

**Destructive** — modify flash, image store, or firmware. Require a snapshot of
the affected region before running and a restore afterward. The harness enforces
this: a destructive test suite must declare what it snapshots, and the runner
verifies snapshot/restore succeeded before and after.

The guiding priority is:
1. EP SWD — everything else depends on it
2. WiFi / connectivity — needed for OTA and log upload
3. OTA reflash — biggest possible disaster if broken
4. EP image store — needed for normal operation

---

## Hardware Prerequisites

- WP flashed with current firmware (no VS Code debug session active)
  - **Always flash WP using the VS Code task "WP: Flash and Run (picotool, partition-aware)"**,
    or the `~/projects/umod4/tools/flash_WP` script directly. Hold the BOOTSEL button
    and press RESET before running — the script waits for the USB device to appear.
  - Both use picotool which writes the ABSOLUTE blocks in `WP.uf2` that establish the
    RP2350 partition table at `0x10000000`. Without a valid partition table,
    `wp_ota_status` reports `available=0` and all OTA tests fail.
  - **Never use OpenOCD `program WP.elf` to flash WP.** The ELF image has `ORIGIN=0x10000000`
    (the partition table region), so OpenOCD overwrites the partition table on every flash.
  - **After a picotool flash, power-cycle WP and the debug probe before using OpenOCD.**
    picotool leaves the QSPI flash chip in Quad I/O (4-wire) mode. OpenOCD's single-wire
    JEDEC probe returns flash ID `0x000000` and fails to connect until the chip is
    power-cycled back to single-wire SPI mode.
- EP flashed with current firmware and running
- Debug probe (Raspberry Pi Debug Probe) connected to WP SWD header
- SD card inserted, filesystem mounted
- EP's SPARE2 pin NOT grounded (allows WP↔EP SWD)

---

## Test Runner Behaviour

The runner (`tests/runner.py`) always resets WP via OpenOCD before starting any
suite. This guarantees a clean boot regardless of what state a previous run left
the firmware in (panic loops, wedged tasks, partial test state). The `wait_ready()`
call then blocks until WP's RTT control block is live before tests begin.

---

## Test Surfaces

### 1. Basic / Sanity (`test_basic.py`) ✓

Verifies the VFY channel itself and basic firmware health. Must pass before
any other suite is meaningful.

| ID       | Description                                 | Destructive | Status    |
|----------|---------------------------------------------|-------------|-----------|
| ping     | VFY channel round-trip                      | No          | ✓ passing |
| version  | Build timestamp present in response         | No          | ✓ passing |
| status   | Heap and LFS mount state reported           | No          | ✓ passing |
| lfs_test | Write/read/verify/delete a temp file on LFS | No          | ✓ passing |

---

### 2. EP SWD (`test_ep_swd.py`) ✓

Verifies the WP↔EP SWD communication path. This is a prerequisite for OTA
testing (Suite 3) and image store testing (Suite 4) because both use SWD to
snapshot and restore EP flash.

All tests in this suite are non-destructive. The suite is structured in three
phases around the EP reset line so it works identically on blank and
programmed boards.

#### Phase 0 — Prerequisites

| ID               | Description                                                  | Destructive | Status    |
|------------------|--------------------------------------------------------------|-------------|-----------|
| swd_spare2_check | Verify SPARE2 pin is high (not grounded); gates entire suite | No          | ✓ passing |

If `swd_spare2_check` fails the suite aborts immediately. SPARE2 must be
ungrounded for WP→EP SWD to function. (SPARE2 is grounded only when an external
debug probe is attached to EP — see Hardware Prerequisites above.)

#### Phase 1 — EP halted in bootrom (blank-board safe, pure SWD driver)

RP2040 SWD does not respond while RUN is held low. The test therefore pulses
EP_RUN (assert 10 ms, release), waits 50 ms for the bootrom, then connects with
halt=true. EP is halted in bootrom for the duration of phase 1.

| ID                      | Description                                                    | Destructive | Status    |
|-------------------------|----------------------------------------------------------------|-------------|-----------|
| swd_connect_in_reset    | Pulse EP_RUN, halt EP in bootrom, verify IDCODE                | No          | ✓ passing |
| swd_read_flash_in_reset | Read EP flash at 0x10000000, report blank (0xFF) vs programmed | No          | ✓ passing |
| swd_ram_roundtrip       | Write/read test pattern to SRAM scratch (0x20020000)           | No          | ✓ passing |
| swd_load_swdreflash     | Load SwdReflash binary to EP RAM, verify readback (no launch)  | No          | ✓ passing |

#### Phase 2 — Flash write (must pass before Phase 3)

Flash write is tested before releasing EP from halt. The rationale: if we
cannot write to flash, we cannot recover from the "bad image" world that
Phase 3 might discover. Proving flash write works first ensures the recovery
path exists before we need it.

The test uses the unused flash region defined in `EP/src/memmap_eprom.ld`
(`__unused_flash_start__`, `__unused_flash_size__`). The harness reads these
symbols from `build/EP/EP` at runtime rather than hardcoding addresses.
The last 64K block of the unused region is used as the scratch target so
that any future forward-allocation from the start of the region doesn't
accidentally collide with the scratch block.

| ID              | Description                                                               | Destructive | Status    |
|-----------------|---------------------------------------------------------------------------|-------------|-----------|
| swd_write_flash | Launch SwdReflash, write pattern to unused-flash scratch, verify readback | No*         | ✓ passing |

*Writes to the last 64K of `UNUSED_FLASH` (defined in the EP linker script).
Does not modify EP code, image store, or any active image slots.

#### Phase 3 — Release reset, verify SWD survives transition

| ID                       | Description                            | Destructive | Status    |
|--------------------------|----------------------------------------|-------------|-----------|
| swd_release_reset        | Pulse EP_RUN, wait 2s for EP to boot   | No          | ✓ passing |
| swd_reconnect_after_boot | Reconnect SWD, verify still responsive | No          | ✓ passing |

`swd_reconnect_after_boot` failure is a hard abort — SWD unresponsive after boot
means a bad image or hung EP.

After phase 3 the suite records which world it is in:

- **Blank EP** — bootloader running; `w0` in reconnect reply will be 0xFF (unprogrammed flash)
- **Programmed EP** — firmware booted; `w0` reflects EP firmware vector table
- **Bad image** — EP hangs or reset-loops; `swd_reconnect_after_boot` FAIL → suite aborts

**Notes:**

- All EP access goes through WP's existing SWD infrastructure (`Swd`, `FlashEp`).
  The harness never has a direct SWD connection to EP. The flow is always:
  harness → VfyTask (RTT) → WP SWD stack → EP. These tests therefore also
  implicitly verify that WP's SWD driver is functioning correctly.
- Suite 2 only verifies the SWD communication path — it makes no claims about
  whether EP firmware is functional. That question is answered by Suite 3
  (ep_runs_after). A blank board can pass all of Suite 2.
- SRAM scratch area: 0x20020000 (mid-SRAM, above swdreflash load area at
  0x20000000 and below the bootrom stack near top of SRAM). DPSRAM (0x50100000)
  is not used here because it requires USB peripheral init which hasn't happened
  in bootrom.
- VfyTask acquires the SWD mutex before every SWD operation to avoid racing
  with the ep_rtt_forwarder task.

**Future: Brick Recovery Test (not yet planned)**
A deliberately-hostile test that loads an EP image which disables its own SWD
port and/or stops its clocks, then verifies WP can recover it. Needs careful
design to avoid a genuinely unrecoverable state if the recovery itself fails.

---

### 3. WiFi / Connectivity (`test_wifi.py`) ✓

Verifies WiFi association and basic HTTP server operation. Required before
WP OTA tests. The `wifi_status` VFY command returns the WP IP address, which
the harness uses for all subsequent HTTP tests — no hardcoded address needed.

| ID           | Description                                          | Destructive | Status    |
|--------------|------------------------------------------------------|-------------|-----------|
| wifi_status  | WiFi SSID, RSSI, and IP reported via RTT VFY channel | No          | ✓ passing |
| http_status  | HTTP GET /api/info returns valid JSON                | No          | ✓ passing |
| http_sd_info | HTTP GET /api/sd-info returns total_mb/used_mb       | No          | ✓ passing |

**Notes:**

- `wifi_status` failure aborts the suite; HTTP tests require a known IP.
- WiFi association happens during WP boot; the runner's mandatory WP reset at
  startup means WiFi is already up by the time Suite 3 runs.

---

### 4. OTA Reflash

Highest-stakes tests — a failure here means a potentially unrecoverable board.
**Prerequisites:** Suite 2 (EP SWD) and Suite 3 (WiFi) must pass first.

#### 4a. EP Reflash (`test_ota_ep.py`) — PARTIAL

Exercises the full end-to-end EP reflash path:
`test harness → server --flash-ep → HTTP upload → WP LFS → FlashEp::flashUf2 → SwdReflash → EP flash → EP reboot`

The server CLI mode (`umod4_server.py --flash-ep <path> --ip <addr>`) is used so the
upload and trigger logic is exercised through the same code path as a real operator reflash.

| ID             | Description                                                      | Destructive | Status    |
|----------------|------------------------------------------------------------------|-------------|-----------|
| ep_reflash     | Upload EP.uf2 and trigger reflash via HTTP API                   | Yes*        | ✓ passing |
| ep_runs_after  | SWD reconnect confirms EP is alive after reflash+reboot          | No          | ✓ passing |
| ep_cleanup     | Delete EP.uf2 from WP LFS via lfs_delete VFY command             | No          | ✓ passing |

*Reflashes EP with the same firmware already running — functionally a no-op for EP
behavior, but exercises the full flash write path. Does not snapshot/restore since
the image is identical.

**Notes:**

- `DeviceClient` is imported directly (not via server subprocess) to avoid PySide6
  dependency. Run the harness with `build/.venv/bin/python3` (has `requests`).
- No post-flash SHA256 check is needed. `MAILBOX_CMD_PGM` in SwdReflash already
  does a byte-for-byte verify of every block on the EP itself after each write,
  with a dedicated `MAILBOX_STATUS_ERR_VERIFY` failure code.
- **Future — `ep_runs_after` enhancement:** extend this test to verify basic EP
  firmware behaviour by watching the ECU log data stream for events that appear
  shortly after every EP reboot (e.g. the EP log-version event). This confirms
  the flashed image actually initialises correctly, not just that SWD can connect.
  Scope: EP firmware events only — HC11 operation is out of scope here and belongs
  in a separate engine test suite.

#### 4b. WP Self-Reflash (`test_ota_wp.py`) — IMPLEMENTED

| ID                 | Description                                                               | Destructive | Status      |
|--------------------|---------------------------------------------------------------------------|-------------|-------------|
| wp_ota_status      | Pre-flight: verify OTA available; report boot_slot and target_slot        | No          | implemented |
| wp_ota_upload      | Upload WP.uf2 to device LFS via HTTP                                      | Yes         | implemented |
| wp_ota_flash_start | Trigger reflash; confirm OTA task began (VFY: wp_ota FLASH_START)         | Yes         | implemented |
| wp_ota_flash_done  | Wait for flash complete (VFY: wp_ota FLASH_DONE, max 120s)                | Yes         | implemented |
| wp_ota_tbyb_reboot | Confirm reboot imminent (VFY: wp_ota TBYB_REBOOT), reconnect OpenOCD      | Yes         | implemented |
| wp_ota_verify      | After reboot, ping + version check on VFY channel                         | No          | implemented |
| wp_ota_cleanup     | Delete WP.uf2 from LFS via lfs_delete VFY command                         | No          | implemented |

**Notes:**

- WP OTA shuts down WiFi before flashing starts — HTTP is unavailable during
  the flash window. The harness tracks progress entirely via VFY ch1 milestones.
- VFY milestone sequence emitted by `ota_flash_task.cpp`:
  1. `VFY: wp_ota FLASH_START file=<path>` — flash programming beginning
  2. `VFY: wp_ota FLASH_DONE target=0x...` — flash complete
  3. `VFY: wp_ota TBYB_REBOOT` — scheduler about to be suspended, `rom_reboot()` imminent
- Progress lines every ~64 KB are also emitted on RTT ch0:
  `WP OTA: <done>/<total> KB (<pct>%)` — visible to anyone watching OpenOCD output.
- After `TBYB_REBOOT`, the harness waits 15s for both reboots (TBYB warm boot +
  cold boot), then calls `ocd.reconnect()` which stops and restarts OpenOCD
  **without** resetting WP — critical because a debug reset would abort TBYB commit.
- Reflash is functionally a no-op for WP behavior (same firmware), but exercises
  the full A/B partition flip, TBYB commit, and watchdog cold reboot path.

---

### 5. EP Image Store (`test_image_store.py`) — PLANNED

Verifies the EP image store — the partition at 0x10200000 that holds EPROM
images and the image selector BSON document.

**Prerequisite:** Suite 2 (EP SWD) must pass first. Destructive tests require
a snapshot of the image store region.

| ID                      | Description                                                        | Destructive | Status  |
|-------------------------|--------------------------------------------------------------------|-------------|---------|
| imgstore_snapshot       | Read image store selector slot (0x10200000, 32KB) via SWD          | No          | planned |
| imgstore_selector_valid | Parse selector BSON, verify structure and at least one image entry | No          | planned |
| imgstore_slot_read      | Read slot 1 raw binary, verify not all-0xFF (slot populated)       | No          | planned |
| imgstore_erase_selector | Erase selector slot, verify EP falls back to limp mode             | Yes         | planned |
| imgstore_restore        | Restore selector from snapshot, verify EP reloads correctly        | Yes         | planned |

**Notes:**

- "Limp mode" = EP runs with a blank image store (no EPROM images loaded).
  This is a known safe state but the engine will not run.
- The erase/restore cycle is the key test — it proves the snapshot/restore
  mechanism works before we trust it to protect OTA tests.

---

## Snapshot / Restore Protocol

Destructive test suites follow this four-phase protocol:

1. **Snapshot** — read the affected flash region via SWD, save to a file on
   LFS (or on the host). Verify the snapshot is complete and non-empty.
2. **Provision** — put the board into the specific state needed for the test.
3. **Test** — run the destructive test.
4. **Restore** — write the snapshot back to flash. Verify EP/WP is running
   correctly afterward.

If Snapshot fails, the destructive test is skipped entirely. If Restore fails,
the runner flags the board as potentially in an unknown state and stops.

---

## Running Order

For a full board qualification, run suites in this order:

```
test_basic → test_ep_swd → test_wifi → test_ota_ep → test_ota_wp → test_image_store
```

Each suite is a gate for the next. A failure in an earlier suite aborts the
later ones.
