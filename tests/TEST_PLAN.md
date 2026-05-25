# umod4 Test Plan

## Goal

The goal is for all testing to be performed from an automated test harness — including
starting from a completely erased device, interacting with WP in AP mode via the ap_proxy,
and exercising every firmware subsystem without any manual steps.

The harness uses a debug probe (CMSIS-DAP) connected to WP's SWD header to flash firmware
and drive the WP_VFY RTT channel, and WP's HTTP API over WiFi for bulk operations (firmware
upload, OTA).  The ap_proxy device provides a WiFi bridge that allows the harness to reach
WP when it is in AP mode before home-WiFi credentials have been configured.

A small number of tests — captive portal behaviour, browser UX, and phone OS interactions —
are intrinsically impossible to automate and remain documented as manual procedures.

---

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

- PC to drive the automated testing
- umod4 board
    - WP connected to USB power **and USB cable to PC** (required for BOOTSEL flashing)
    - Micro SD Card inserted
    - SPARE2 pin must be floating (i.e. not grounded!)
    - Raspberry Pi SWD Debug probe connected to WP SWD header
- ap_proxy (PicoW) board
    - ap_proxy firmware must be installed (VID:PID `1209:0001` after firmware flash)
    - ap_proxy must be connected via USB cable to PC that is driving the tests

On WSL, USB device attachment is handled automatically — see [USB Device Setup (WSL)](#usb-device-setup-wsl) below for the required one-time setup.

There is no requirement to pre-install __any__ software on the umod4.

## Software Prerequisites

The `umod4_server.py` GUI **must not be running** during automated testing.  If it is
running it will respond to WP's UDP auto-discovery broadcast, causing WP to save the
server's IP to its config partition and corrupting test state.

The test runner handles this automatically: at startup it checks for a running server
process and terminates it (`pkill -f umod4_server.py`) with a logged warning before
proceeding.  It optionally restarts the server after all tests complete.

---

## USB Device Setup (WSL)

On WSL, USB devices must be shared from Windows via `usbipd`.  This is a **one-time
setup** — run `tools/setup_usb_wsl.ps1` once as Administrator.  The easiest way is
from the WSL terminal in the project root:

```bash
powershell.exe -Command "Start-Process powershell -Verb RunAs -ArgumentList '-File \"$(wslpath -w $(pwd)/tools/setup_usb_wsl.ps1)\"'"
```

This triggers a UAC elevation prompt and runs the script as Administrator.

This adds AutoBind policies for:

- `2e8a:000c` — CMSIS-DAP debug probe (Raspberry Pi Debug Probe)
- `1209:0001` — ap_proxy (pid.codes reserved test PID — see ap_proxy firmware)

After this, devices bind automatically whenever plugged in.  No further manual
`usbipd` steps are needed: VS Code attaches and detaches the debug probe around
each debug session, and the test runner manages attachment during test runs.

**Note:** The ap_proxy VID:PID (`1209:0001`) is the [pid.codes reserved test PID](https://pid.codes/1209/0001/).
It is unique enough for automated usbipd policy matching but must not appear on
any device that is redistributed, sold, or manufactured.

---

## Tool Reference

### `tools/wp_enter_bootsel`

Puts WP into USB BOOTSEL (mass-storage) mode via SWD, without pressing any buttons.
Requires the debug probe and OpenOCD.  **A USB cable must be connected from WP to the
PC** — SWD can enter BOOTSEL mode but the RP2350 needs USB to enumerate as a mass-storage
device.  On WSL2 the script verifies the RP2350 Boot device appeared in usbipd after
triggering BOOTSEL, and exits non-zero with a clear error message if it does not.

```
tools/wp_enter_bootsel build/WpUsbBoot/WpUsbBoot
```

This is how the automated harness enters BOOTSEL mode — used by both `runner.py` and
`flash_WP` internally.

### `tools/flash_WP`

Flashes WP firmware.  Assumes WP is already in BOOTSEL mode.

```
tools/flash_WP          build/WP    # flash only (preserves flash config partition)
tools/flash_WP -e       build/WP    # erase then flash (blank-slate)
```

`-e` erases all flash including the config partition (WiFi credentials, device name,
server address).  Use it when you need a known-blank starting point.

On WSL2 the script handles usbipd attach/re-attach automatically.

### `tools/flash_EP`

Flashes EP firmware over WiFi via WP's HTTP API.  Unlike `flash_WP`, no BOOTSEL mode or
physical USB connection to EP is required — WP receives the image over WiFi and programs
EP via its internal SWD connection.

```bash
tools/flash_EP umod4_garage                          # release mode (EP.uf2 beside script)
tools/flash_EP umod4_garage ~/projects/umod4/build/EP  # dev mode
```

Prerequisites: WP must be running and connected to home WiFi.  The device is located via
mDNS (`<device-name>.local`).  The SD card must be inserted (WP uses it as staging storage
for the EP image before programming).

This is the end-user tool for EP firmware updates.  The automated test harness exercises
the same code path via `test_ota_ep` using the HTTP API directly.

### `tools/ap_proxy`

Allows the test harness software running on a PC to connect to a umod4 running in AP
(Access Point) mode, then supply the WiFi credentials to allow the umod4 to connect to
the home network.

When powered up, ap_proxy continuously scans for WiFi SSIDs that correspond to a umod4
in AP mode.  When one is found, it connects to the AP using the default AP password
(same as the SSID).  The ap_proxy software uses its USB serial connection as a proxy
for the host PC to send and receive WiFi traffic to the umod4 in AP mode.

---

## How the Test Harness Connects

### RTT Channels (via OpenOCD)

OpenOCD exposes each RTT channel as a TCP server.  `harness/rtt.py` is used in test
code; `nc` is useful for quick manual verification:

```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
    -c "rtt setup 0x20000000 0x80000 \"SEGGER RTT\"" \
    -c "rtt start" \
    -c "rtt server start 9090 0" \   # WP_STDIO  (debug output, read-only)
    -c "rtt server start 9091 1" \   # WP_VFY    (test commands in, results out)
    -c "rtt server start 9092 2"     # WP_SHELL  (human interactive shell)

# Manual test command
echo "boot" | nc -q1 localhost 9091
# → {"boot":{"slot":1,"built":"May 12 2026 10:30:00"}}
```

`nc` timeout behavior varies across Linux versions; always use `harness/rtt.py` in scripts.

### HTTP API (via WiFi)

Once WiFi is running, the harness calls WP's HTTP server directly.  Used for bulk
operations (firmware upload, file management) that are awkward over RTT.  The
`wifi` VFY command returns the WP IP address — no address is ever hardcoded.

---

## VFY Channel Protocol

The WP_VFY channel (RTT channel 1) is the primary automation interface.  Fully
bidirectional, ASCII on both sides — drive it with `nc` for manual debugging.

**Command format (host → device):**

```
<command> [arg1] [arg2] ...
```

**Response format (device → host):**

Every response is a single-line JSON object terminated with `\n`.  The top-level key
is always the command name; the value is an object with a `state` field plus any
command-specific fields.

Status commands report current state, no side effects:

```json
{"wifi":{"state":"connected","ssid":"MyNet","ip":"192.168.1.42","rssi":-55}}
{"filesystem":{"state":"mounted","reformatted":false,"mount_ms":312}}
```

Test commands report the operation result via `state`:

```json
{"swd_test_connect":{"state":"ready"}}
{"swd_test_flash":{"state":"ok"}}
```

Error states use descriptive strings rather than a generic code:

```json
{"swd_test_flash":{"state":"flasher_timeout"}}
{"filesystem":{"state":"not_mounted"}}
```

The `status` aggregate calls all status commands and wraps their output:

```json
{"status":{"boot":{...},"heap":{...},"wifi":{...},"swd":{...},...}}
```

**Important:** `vfy_printf()` uses `BLOCK_IF_FIFO_FULL`.  Only call it in response to an
explicit command — never unconditionally during boot or normal operation.  If no
debugger is draining the buffer, the calling task blocks permanently.  Boot status
belongs on WP_STDIO.

### VFY Command Naming Convention

Commands follow a two-category scheme:

- **`xxx`** — *status*: reads current state of subsystem `xxx`, no side effects.
- **`xxx_test_<name>`** — *test*: active operation on subsystem `xxx`, named `<name>`.
  Has a result; side effects are permitted.
- **`status`** — *aggregate*: calls all status commands; returns their combined output.

The `state` field is always present.  For status commands it describes the current
condition (`"connected"`, `"mounted"`, `"ready"`, `"ap_mode"`, etc.).  For test commands
it reports the outcome — `"ok"` or `"ready"` on success, or a failure reason string
(`"flasher_timeout"`, `"not_mounted"`) on failure.  The test harness always makes the
pass/fail decision based on `state`; the firmware only reports facts.

### Full Command Set

| Command | Type | Purpose | Status |
| --- | --- | --- | --- |
| `status` | aggregate | All subsystem status in one response | ✓ passing |
| `boot` | status | Boot slot, build timestamp | ✓ passing |
| `heap` | status | Free heap statistics | ✓ passing |
| `wifi` | status | WiFi state, SSID, IP address, RSSI | ✓ passing |
| `swd` | status | Cached boot-time SWD connectivity state | ✓ passing |
| `ep` | status | EP boot, UART data flowing, image loaded, ECU started | planned |
| `ecu` | status | ECU E-clock frequency, event counts, metadata | ✓ passing |
| `gps` | status | GPS fix state, satellite count | ✓ passing |
| `sd` | status | SD card detected, capacity | ✓ passing |
| `filesystem` | status | LFS mount state, mount time | ✓ passing |
| `ota` | status | OTA boot slot, target slot | ✓ passing |
| `swd_test_connect` | test | Live SWD round-trip: write/read pattern to EP SRAM | ✓ passing |
| `swd_test_flash` | test | Full SWD flash cycle: load stub → write scratch block → release EP → verify alive | ✓ passing |
| `filesystem_test_rw` | test | LFS write/read/verify/delete a temp file | ✓ passing |
| `filesystem_test_delete <file>` | test | Delete named file from LFS | ✓ passing |

---

## Test Runner

```bash
# Full run: erase config partition, flash latest firmware, provision WiFi, run all suites
build/.venv/bin/python3 tests/runner.py --ssid <network> --password <pw> --device-name <name>

# Partial run: flash latest firmware (config partition preserved), run specific suites
build/.venv/bin/python3 tests/runner.py test_basic test_ep_swd test_wifi test_ota_ep test_ota_wp

# Run a single suite (board must already be provisioned)
build/.venv/bin/python3 tests/runner.py test_basic
```

The runner always flashes the latest WP firmware.  The only difference between a full
run and a partial run is whether `flash_WP -e` (erase + flash, destroys config partition)
or `flash_WP` (flash only, config partition preserved) is used.

The runner (`tests/runner.py`) always:

1. Runs `wp_enter_bootsel` + `flash_WP` (or `flash_WP -e` when provisioning) to program the latest build
2. Starts OpenOCD and waits for RTT to come up on WP
3. Sleeps 5 s to let the TBYB commit + cold-boot cycle complete
4. Runs each suite in order, passing a shared `context` dict between them
5. Prints a pass/fail summary and exits 0 (all pass) or 1 (any fail)

### Pre-flight Checks

The runner executes these checks before flashing or opening any RTT connection.
Failures marked **abort** halt the run immediately with a descriptive error; others
warn or auto-resolve.

| Check | What is verified | On failure |
| --- | --- | --- |
| `server_kill` | `umod4_server.py` is not running | Auto-kill with warning |
| `ap_proxy_usb` | ap_proxy device (`1209:0001`) is visible on the USB bus (any usbipd state) | Abort |
| `cmsis_dap_usb` | CMSIS-DAP debug probe (`2e8a:000c`) is visible on the USB bus (any usbipd state) | Abort |
| `cmsis_dap_attach` | (WSL only) CMSIS-DAP attached to WSL via `usbipd` (runner, after preflight) | Fatal |
| `openocd_present` | `openocd` is on PATH and `openocd --version` exits 0 | Abort |
| `picotool_present` | `picotool` is on PATH and `picotool version` exits 0 | Abort |
| `wp_uf2` | `build/WP/WP.uf2` exists | Abort |
| `ep_uf2` | `build/EP/EP.uf2` exists | Abort |
| `wpusbboot_bin` | `build/WpUsbBoot/WpUsbBoot` binary exists | Abort |
| `python_venv` | `build/.venv/bin/python3` exists and `requests` package is importable | Abort |
| `args_wifi` | `--ssid` and `--password` command-line arguments are present | Abort |
| `args_device_name` | `--device-name` command-line argument is present | Abort |

**WSL detection:** `/proc/version` is checked for "microsoft".  When WSL is detected,
`usbipd list` is called to check device state by matching VID:PID against the STATE
column (`Not shared` / `Shared` / `Attached`).  Preflight only verifies presence —
it does not attach.  The runner attaches the debug probe (`2e8a:000c`) immediately
after preflight and detaches it on exit.  The `test_provisioning` suite attaches the
ap_proxy (`1209:0001`) when it needs it; the ap_proxy is never explicitly detached.

**Limitation:** If more than one debug probe (`2e8a:000c`) is connected simultaneously,
the VID:PID match will hit the first one found in `usbipd list` output.  The harness
is designed for a single debug probe.  Connecting a second probe during a test run
will produce unpredictable results.

---

### Board State and Test Flow

The harness assumes a **dedicated test unit** — a umod4 board reserved exclusively for
automated testing that can be fully erased at any time.  No snapshot or restore of the
WP config partition, EP image library, or image selector is performed by default.

The harness follows a two-phase sequence:

**Phase 1 — Provision:** Erase WP flash entirely and bring the board from zero state
through WiFi provisioning to home-network connectivity (Suite 0).  At the end of this
phase the board has WP firmware running, a device name configured, home WiFi credentials
stored in the config partition, and an IP address on the home network stored in `context`.

**Phase 2 — Run All Tests:** With the board in a known provisioned state, run suites 1–6
in order.  Each suite gates the next.  The EP image store suite (Suite 6) erases and
restores only the EP image store, not WP config.

After a completed run the board is left provisioned with the test WiFi credentials and
device name — the next run starts from Phase 1 and erases everything anyway.

> **Note:** The original four-phase snapshot/restore model (Snapshot → Provision → Test → Restore)
> is supported via `--restore-board` for cases where a production board must be returned to
> its original state.  It is not needed for normal automated testing against a dedicated unit.

---

## Automated Test Suites

### Running Order

```text
test_provisioning → test_basic → test_ecu → test_ep_swd → test_wifi → test_ota_ep → test_ota_wp → test_image_store
```

`test_provisioning` runs first and gates all subsequent suites — if the device cannot
be brought up and connected to the home network, nothing else is meaningful.

Each subsequent suite gates the next.  A failure in an earlier suite aborts the later ones.

---

### 0. Provisioning / First-Run (`test_provisioning.py`) ✓

Brings a WP from **zero state** (flash fully erased — no firmware, no config partition,
no partition table) through WiFi provisioning to home-network connectivity.  This is the
automated equivalent of a new-user first-run setup and gates every subsequent suite.

**Zero state definition:** WP flash is 0xFF throughout.  The RP2350 bootrom has no valid
image to launch, so it may or may not enumerate on USB automatically; the harness does not
rely on this.  The CMSIS-DAP SWD connection is sufficient to enter BOOTSEL regardless of
firmware state.

**Requires:** All pre-flight checks passed (see above), including ap_proxy attached.

| ID | Description | Destructive | Status |
| --- | --- | --- | --- |
| prov_enter_bootsel | Put WP into BOOTSEL mode via SWD (`wp_enter_bootsel`) | No | ✓ passing |
| prov_flash_wp | Erase all WP flash and write latest WP.uf2 (`flash_WP -e`) | Yes | ✓ passing |
| prov_ocd_restart | Start OpenOCD and wait for RTT; settle for post-flash boot cycle | No | ✓ passing |
| prov_ap_boot | `wifi` VFY command confirms `state == "ap_mode"` | No | ✓ passing |
| prov_ap_connect | ap_proxy scans for `umod4_XXXX`, connects using default password (= SSID) | No | ✓ passing |
| prov_set_wifi | ap_proxy POST device name + WiFi credentials to `/api/config` | Yes | ✓ passing |
| prov_wait_reboot | Wait for WP config-save reboot; reconnect OpenOCD | No | ✓ passing |
| prov_wifi_connect | Poll `wifi` VFY command until `state == "connected"` (60 s timeout) | No | ✓ passing |

**Notes:**

- `--ssid`, `--password`, and `--device-name` are required `runner.py` arguments; never hardcoded.
- Default AP password = device name = AP SSID (e.g. `umod4_3BFF`).  Generated from MAC
  bytes on first boot, written to both `ap_ssid` and `ap_password` in flash config.
- `prov_set_wifi` posts all three fields (device name, SSID, password) in a single request
  to `/api/config`.  WP saves to flash and reboots; the AP disappears immediately after.
- After this suite passes WP is on the home network; subsequent suites communicate with it
  via RTT (SWD through the CMSIS-DAP) and HTTP (via `context['wp_ip']`).
- EP is not flashed during provisioning.  Suite 3 (EP SWD) verifies the SWD path; Suite 5a
  (OTA EP) flashes EP firmware via the WP HTTP API.

---

### 1. Basic / Sanity (`test_basic.py`) ✓

Verifies the VFY channel itself and basic firmware health.  Must pass before any other
suite is meaningful.

| ID | Description | Destructive | Status |
| --- | --- | --- | --- |
| `boot` | Boot slot and build timestamp present in response | No | ✓ passing |
| `heap` | Free heap reported | No | ✓ passing |
| `sd` | SD card detected and operational; **fatal** if not — no OTA without it | No | ✓ passing |
| `filesystem_test_rw` | Write/read/verify/delete a temp file on LFS; **fatal** if fails | No | ✓ passing |

---

### 2. ECU Status (`test_ecu.py`) ✓

Verifies ECU firmware identity and correct operation.  Uses the `ecu` VFY command, which
reports data forwarded from EP over the UART link.

ECU power is detected via E-clock frequency: if `eclk_khz` is below 1800 kHz the ECU is
unpowered and all ECU-specific tests are skipped with a `ecu_not_powered` PASS result.
Otherwise the suite waits 30 s for the ECU to accumulate log events before sampling.

| ID | Description | Destructive | Status |
| --- | --- | --- | --- |
| `ecu_not_powered` | ECU unpowered (eclk_khz below threshold) — **aborts suite**, runner continues | No | ✓ passing |
| `ecu_powered` | ECU powered; continues to accumulation phase | No | ✓ passing |
| `ecu_metadata` | ECU firmware identity (build tag, date) present | No | ✓ passing |
| `cpu_events` | Exactly 1 RESET CPU event, no others | No | ✓ passing |
| `t1_overflows` | T1 timer overflows > 0 (ECU main loop running) | No | ✓ passing |
| `aap_count` | AAP sample count (informational, always passes) | No | ✓ passing |
| `vm_count` | VM event count (informational, always passes) | No | ✓ passing |
| `error_l000c` | Error register at 0xL000C (informational, always passes) | No | ✓ passing |

**Note:** This suite requires the motorcycle ECU to be installed and the engine running
(or at least cranking) to advance past `ecu_powered`.  On a bench without an ECU the
suite terminates cleanly at `ecu_not_powered`.

---

### 3. EP SWD (`test_ep_swd.py`) ✓

Verifies the WP↔EP SWD communication path and the SWD flash-programming mechanism.
Prerequisite for OTA EP testing (Suite 5a) and EP image store testing (Suite 6).

All EP access goes through WP's SWD driver: harness → VfyTask (RTT) → WP SWD → EP.
These tests therefore also implicitly verify WP's SWD infrastructure.  A blank EP board
can pass all three tests.

SPARE2 must be floating for WP→EP SWD to function.  If SPARE2 is grounded (indicating
an external probe is attached to EP), the `swd` command reports `state == "inhibited"`
and the suite aborts.

| ID | Description | Destructive | Status |
| --- | --- | --- | --- |
| `swd` | Cached boot-time SWD connectivity state; aborts suite if not `"ready"` | No | ✓ passing |
| `swd_test_connect` | Live SWD round-trip: write test pattern to EP SRAM, read back, verify | No | ✓ passing |
| `swd_test_flash` | Full SWD flash cycle: load flasher stub → write 64K scratch block → release EP from reset → verify EP alive | No* | ✓ passing |

*Writes to the last 64K of `UNUSED_FLASH` (derived from `__unused_flash_start__` and
`__unused_flash_size__` symbols in EP.elf).  Does not touch EP code, image store, or
active partition slots.

**Why `swd_test_flash` matters:** It proves the SWD flash-programming mechanism works
end-to-end from WP to EP and back.  This is the underpinning of both EP OTA (`test_ota_ep`)
and EP image store manipulation (`test_image_store`): if `swd_test_flash` fails, those
suites cannot be trusted.

**`swd_test_flash` implementation:** A single firmware VFY command that runs the full
three-phase sequence internally: load flasher stub into EP SRAM → program scratch block →
release EP from reset → reconnect SWD and verify EP alive.  The scratch address is derived
by the test harness from `__unused_flash_start__` and `__unused_flash_size__` ELF symbols
and passed as an argument: `swd_test_flash 0x<addr>`.

---

### 4. WiFi / Connectivity (`test_wifi.py`) ✓

Verifies WiFi association and HTTP server operation.  Required before OTA tests.
The `wifi` command stores `wp_ip` in `context` for subsequent suites.

| ID | Description | Destructive | Status |
| --- | --- | --- | --- |
| `wifi` | WiFi state, SSID, RSSI, and IP reported; aborts if not `"connected"` | No | ✓ passing |
| `http_status` | HTTP GET `/api/info` returns valid JSON | No | ✓ passing |
| `http_sd_info` | HTTP GET `/api/sd-info` returns `total_mb`/`used_mb` | No | ✓ passing |

`wifi` failure aborts the suite; HTTP tests require a known IP stored in `context['wp_ip']`.

---

### 5. OTA Reflash

Highest-stakes tests — a failure here means a potentially unrecoverable board.
**Prerequisites:** Suite 3 (EP SWD) and Suite 4 (WiFi) must pass first.

#### 5a. EP Reflash (`test_ota_ep.py`) ✓

`harness → DeviceClient → HTTP upload → WP LFS → FlashEp::flashUf2 → SwdReflash → EP flash → EP reboot`

| ID | Description | Destructive | Status |
| --- | --- | --- | --- |
| `ep_reflash` | Upload EP.uf2 and trigger reflash via HTTP API | Yes* | ✓ passing |
| `ep_runs_after` | `swd_test_connect` confirms EP is alive after reflash+reboot | No | ✓ passing |
| `ep_cleanup` | `filesystem_test_delete EP.uf2` removes file from WP LFS | No | ✓ passing |

*Reflashes EP with the same firmware — exercises the full flash write path without
changing behaviour.  No snapshot/restore needed since the image is identical.

**Notes:**

- `DeviceClient` is imported directly (not via server subprocess) to avoid PySide6 dependency.
  Run the harness with `build/.venv/bin/python3` (has `requests`).
- No post-flash SHA256 check needed: `MAILBOX_CMD_PGM` in SwdReflash already does a
  byte-for-byte verify of every block on the EP after each write.
- **Future:** extend `ep_runs_after` to verify ECU log data stream events after reboot,
  confirming the flashed image actually initialises (not just that SWD can connect).

#### 5b. WP Self-Reflash (`test_ota_wp.py`) ✓

| ID | Description | Destructive | Status |
| --- | --- | --- | --- |
| `wp_ota_status` | Pre-flight: `ota` command confirms OTA available; captures `boot_slot` and `target_slot` | No | ✓ passing |
| `wp_ota_upload` | Upload WP.uf2 to device LFS via HTTP | Yes | ✓ passing |
| `wp_ota_flash_start` | Trigger reflash; confirm OTA task began (`{"wp_ota":"FLASH_START",...}`) | Yes | ✓ passing |
| `wp_ota_flash_done` | Wait for flash complete (`{"wp_ota":"FLASH_DONE",...}`, max 120 s) | Yes | ✓ passing |
| `wp_ota_tbyb_reboot` | Confirm reboot imminent (`{"wp_ota":"TBYB_REBOOT"}`), reconnect OpenOCD | Yes | ✓ passing |
| `wp_ota_verify` | After reboot, `boot` + `ota` check on VFY channel; confirm new boot slot | No | ✓ passing |
| `wp_ota_cleanup` | `filesystem_test_delete WP.uf2` removes file from LFS | No | ✓ passing |

**VFY milestone sequence** emitted by `ota_flash_task.cpp`:

1. `{"wp_ota":"FLASH_START","file":"<path>"}` — flash programming beginning
2. `{"wp_ota":"FLASH_DONE","target":"0x..."}` — flash complete, reboot imminent
3. `{"wp_ota":"TBYB_REBOOT"}` — scheduler suspended, `rom_reboot()` imminent

After `TBYB_REBOOT`, wait 15 s for both reboots (TBYB warm boot + cold boot), then
call `ocd.reconnect()` **without** resetting WP — a debug reset would abort TBYB commit.

---

### 6. EP Image Store (`test_image_store.py`) — PLANNED

Verifies the EP image store at `0x10200000` (image selector BSON doc + 32KB image slots).
**Prerequisite:** Suite 3 (EP SWD) must pass first, specifically `swd_test_flash` which
proves the SWD flash-programming mechanism that this suite depends on.

| ID | Description | Destructive | Status |
| --- | --- | --- | --- |
| `imgstore_snapshot` | Read image store selector slot (0x10200000, 32KB) via SWD | No | planned |
| `imgstore_selector_valid` | Parse selector BSON, verify structure and at least one image entry | No | planned |
| `imgstore_slot_read` | Read slot 1 raw binary, verify not all-0xFF (slot populated) | No | planned |
| `imgstore_erase_selector` | Erase selector slot, verify EP falls back to limp mode | Yes | planned |
| `imgstore_restore` | Restore selector from snapshot, verify EP reloads correctly | Yes | planned |

"Limp mode" = EP runs with blank image store — engine will not run but board is recoverable.
The erase/restore cycle proves the snapshot mechanism works before it protects OTA tests.

---

## Manual Tests (WiFi / First-Run / UI)

These tests cover first-time setup, WiFi UX, and browser behaviour that cannot be
driven by RTT or HTTP alone.  A phone is required.

**Common preconditions:**

- WP built and flashed
- Server (`tools/server/umod4_server.py`) running on PC for any STA-mode test

---

### T1 — First-run flash of a completely erased Pico2W

**What it tests:** `flash_WP -e` succeeds on a blank device where no partition table
has ever been written to flash.

```bash
tools/wp_enter_bootsel build/WpUsbBoot/WpUsbBoot
tools/flash_WP -e build/WP
```

**Pass:** Script exits 0.  Within ~10 s the AP WiFi network `umod4_XXXX` is visible
on a nearby phone or laptop.

**Fail indicators:**

- `wp_enter_bootsel` non-zero → check debug probe is connected and WP is powered
- `wp_enter_bootsel` "RP2350 Boot device did not appear" → check USB cable is connected from WP to PC
- `flash_WP` "No RP2350 Boot device found" → re-enter BOOTSEL and retry immediately
- SSID never appears → firmware running but WiFiManager not starting; check RTT

**Automation potential:** Flash steps already scripted.  AP appearance checked via
`wifi` VFY poll after boot (`state == "ap_mode"`).

---

### T2 — AP mode stability

**What it tests:** WP stays in AP mode indefinitely when no WiFi credentials are
configured.  A previous bug caused AP→STA→AP thrash.

**Precondition:** WP has blank config — freshly erased (T1) or reset via browser UI.

**Procedure:** Power on WP with blank config.  Wait 60 s.  The SSID `umod4_XXXX`
should remain visible throughout.

**Pass:** SSID stable for 60 s.
**Fail:** SSID disappears and reappears — thrash loop still present.

**Automation potential:** Could poll `wifi` VFY for 60 s after a `wifi_reset`
VFY command (not yet implemented).

---

### T3 — Captive portal

**What it tests:** Connecting a phone to the AP triggers the "Sign in to network"
notification, which opens `wifi_config.html` automatically.

**Precondition:** WP in AP mode (blank config).

**Procedure:** On phone, connect to `umod4_XXXX`.  Wait up to 10 s.

**Pass (iOS/macOS):** "Sign in to network" notification → tapping opens `wifi_config.html`.

**Pass (Android, Chrome/Edge):** Same notification and result.
DuckDuckGo browser does not support captive portal detection — known browser limitation,
not a firmware bug.

**Fail:** No notification after 30 s.  Navigate to `http://1.1.1.1` manually — if that
redirects to `wifi_config.html`, DNS and HTTP redirect are working but the OS probe did
not fire.  Toggle WiFi off/on to reset OS cache and retry.

**Automation potential:** Cannot be automated — requires real phone OS.

---

### T4 — Settings page: device name required

**What it tests:** `wifi_config.html` shows Device Identity at the top, pre-fills the
current name, and blocks save when the name field is empty.

**Procedure:**

1. Open `http://<device-ip>/wifi_config.html`
2. Verify "Device Identity" is the first section (red `*` on label)
3. Verify Device Name field is pre-filled (e.g. `umod4_3BFF`)
4. Clear the field and tap "Save & Reboot" — error appears, device does NOT reboot
5. Enter a name (e.g. `Robin's Tuono`) and tap "Save & Reboot" — device reboots, name persists

**Automation potential:** HTML fetch + parse automatable.  JavaScript save-validation
requires a browser.

---

### T5 — Status page: read-only

**What it tests:** `status.html` is a pure status page with no editable inputs and a
Settings link to `wifi_config.html`.

**Procedure:**

1. Open `http://<device-ip>/status.html`
2. Verify ribbon says "Status" (not "Settings")
3. Verify Device Name, MAC Address, Uptime show real values (not `--`)
4. Verify Server Address shows discovered server IP:port
5. Verify grey "Settings" button links to `wifi_config.html`
6. Verify red "Reboot Device" button present; no "Save & Reboot" button

**Automation potential:** Fetch page + check for no `<input` tags.  Not yet implemented.

---

### T6 — UDP auto-discovery

**What it tests:** WP broadcasts a UDP discovery packet and saves the server IP:port
automatically — no compile-time or manually entered address required.

**Precondition:** WP has home WiFi credentials stored.  Flash config has no saved
server address (freshly erased, or cleared via `flash_WP -e`).

**Procedure:**

1. Start `umod4_server.py` on PC
2. Power on WP — it connects to home WiFi
3. Within 30 s a new device entry appears in the server GUI
4. Open `status.html` — Server Address shows PC's IP:port
5. Reboot WP — address is saved; device connects immediately on next boot without re-discovery

**Pass:** Device appears in server GUI without manual config.  Address persists across reboot.

**Fail step 3:** Device doesn't appear → check server is running and WP is on same subnet;
RTT will show "sendDiscoveryBroadcast" log lines if the broadcast is firing.

**Automation potential:** Python UDP socket listener + `/api/config` poll.  Not yet implemented.

---

### T7 — Full first-time setup (end-to-end)

**What it tests:** Complete new-user experience from blank device through server discovery.

**Procedure:**

1. `tools/wp_enter_bootsel build/WpUsbBoot/WpUsbBoot`
2. `tools/flash_WP -e build/WP`
3. Phone connects to `umod4_XXXX` — captive portal appears (T3)
4. In `wifi_config.html`: enter device name, home WiFi SSID, password → Save & Reboot
5. Reconnect phone to home WiFi
6. Server GUI shows new device with name and IP
7. `status.html` shows name, MAC, server address, uptime

**Pass:** Complete flow without typing any IP address anywhere.

**Fail:** Use T1–T6 to isolate which step failed.

---

## Snapshot / Restore Protocol

Destructive test suites follow this four-phase protocol:

1. **Snapshot** — read the affected flash region via SWD, save to host filesystem.
   Verify complete and non-empty.
2. **Provision** — put the board into the specific state needed for the test.
3. **Test** — run the destructive test.
4. **Restore** — write the snapshot back to flash.  Verify EP/WP running correctly afterward.

If Snapshot fails, the destructive test is skipped entirely.  If Restore fails,
the runner flags the board as potentially in an unknown state and stops.

### Two Snapshot Regions

#### WP Config Partition

Contains WiFi credentials, device name, and server address.  Destroyed by `flash_WP -e`.
Any test suite that erases WP flash must snapshot this first and restore it last.

**Mechanism — picotool in BOOTSEL mode:**

```bash
# Snapshot (WP already in BOOTSEL mode from wp_enter_bootsel)
picotool save -r <config_start> <config_end> wp_config.bin

# Restore (put WP back into BOOTSEL, write partition, reboot)
wp_enter_bootsel
picotool load wp_config.bin --offset <config_start>
picotool reboot
```

picotool has direct flash read/write access in BOOTSEL mode and does not require
the firmware to be running.  Since `wp_enter_bootsel` is always the first step
before any erase/flash operation, the snapshot slots in naturally at that point
with no extra reboot needed.

**Note:** picotool's `--help` does not show them, but it does support `--bus <bus>`
and `--address <addr>` under its target device selection options.  If multiple
RP2350 devices are in BOOTSEL simultaneously (all appear as `2e8a:0003`), these
flags can target a specific physical USB port unambiguously.  On a fixed test rig
the bus/address of the WP slot is stable.

**Locating the partition:**

The config partition address and size are compile-time constants in `FlashConfig.h`.
They are mirrored as a single constant in `tests/harness/flash_layout.py`.  If the
partition layout ever changes, both are updated together.  No runtime discovery is
needed — the partition layout is fixed at build time and does not move between
firmware versions.

#### EP Image Store

Contains the image selector BSON doc and 32KB EPROM image slots at `0x10200000` on EP
flash.  Only relevant to `test_image_store`.

**Mechanism:** Read via WP→EP SWD path using the existing `epread` VFY command
(already implemented).  Restore via the same path.  No OpenOCD direct connection to
EP is needed — WP acts as the SWD bridge.

Size to snapshot: the full image store region (256 × 32KB = 8MB) is impractical.
Snapshot only the image selector slot (slot 0, 32KB at `0x10200000`) plus any
populated image slots identified in the selector BSON.  Unpopulated slots (all-0xFF)
need not be saved.

---

## Automation Roadmap

| Test | Current status | What is needed to automate |
| --- | --- | --- |
| T1 first-run flash | Flash steps scripted; AP appearance not checked | `wifi` VFY poll after boot (`state == "ap_mode"`) |
| T2 AP stability | Manual | Add `wifi_reset` VFY command; poll `wifi` for 60 s |
| T3 Captive portal | Manual — always | Requires real phone OS; cannot be automated |
| T4 Settings page | Manual | HTTP fetch + HTML parse in test suite |
| T5 Status page | Manual | HTTP fetch + HTML parse in test suite |
| T6 UDP discovery | Manual | Python UDP socket listener + `/api/config` poll |
| T7 End-to-end | Manual for phone steps | Flash + discovery automatable; phone steps stay manual |

Highest-value additions:

1. **`wifi_reset` VFY command** — lets tests put WP into blank-config state without
   reflashing.  Enables T2 and parts of T7 to be scripted.
2. **UDP discovery test** — a Python socket listener can verify T6 end-to-end.
3. **HTML structure checks** — `urllib.request` fetch + regex covers T4/T5 structural
   verification without a browser.
4. **`ep` status command** — reports EP boot state, UART data flowing, image loaded,
   ECU started.  Enables automated verification of the EP→WP data path without
   requiring a running engine.
