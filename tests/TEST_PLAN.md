# umod4 Test Plan

This document explains how the automated test harness works and why it is built the way it is.
[README.md](README.md) explains how to run it.

The details that change often are deliberately not copied here, because copies drift.
[Where the details live](#where-the-details-live) says where to find the real ones.

## Goal

The goal is for all testing to be performed from an automated test harness, including starting from a completely erased device, interacting with the WP in AP mode via the ap_proxy, and exercising every firmware subsystem without any manual steps.

A small number of tests (captive portal behaviour, browser UX, phone OS interactions) are intrinsically impossible to automate.
They remain documented as [manual tests](#manual-tests).

## Philosophy

The harness assumes a **dedicated test unit**: a umod4 board reserved for automated testing that can be fully erased at any time.
A normal full run erases the WP's flash and provisions the board from scratch, so nothing on the board survives a run.
The harness does not snapshot or restore the WP's configuration, and a board holding anything you care about must not be used.

Tests fall into two categories:

* **Non-destructive**: safe to run at any time.
  They only read state, or use scratch areas that do not affect the system.
  These form the baseline "is the board healthy?" check.
* **Destructive**: they modify flash, firmware or configuration.
  They only make sense on the dedicated test unit.

Suites are ordered so that the things everything else depends on are tested first:

1. Provisioning: if the board cannot be brought up and connected, nothing else is meaningful.
2. The WP's own health (SD card, filesystem).
3. EP SWD, because the WP reflashes the EP over SWD.
4. WiFi, because OTA uploads use the WP's HTTP server.
5. OTA reflash last, because it is the biggest possible disaster if broken, and because the WP reboots at the end and ends the run.

## How the harness reaches the board

A PC drives everything.
There are three paths to the board:

| Path | Hardware | Used for |
| --- | --- | --- |
| RTT over SWD | CMSIS-DAP debug probe on the WP's SWD header, plus OpenOCD | Flashing, test commands and results, capturing the WP's debug output |
| HTTP over WiFi | The WP's own web server, on the home network | Bulk operations: firmware upload, file management |
| AP-mode proxy | The ap_proxy (a Pico W on USB) | Reaching the WP while it is in AP mode, before it has any home-WiFi credentials |

### RTT channels

The runner starts its own OpenOCD, which finds the RTT control block in the WP's RAM.
OpenOCD exposes each RTT channel as a TCP server on port `9000 + channel number`.
Test code uses `harness/rtt.py` to talk to those ports.

| Channel | Port | Name | Direction | Purpose |
| --- | --- | --- | --- | --- |
| 0 | 9000 | WP_STDIO | up only | WP debug printf output |
| 1 | 9001 | WP_VFY | bidirectional | Test automation commands and results |
| 2 | 9002 | WP_SHELL | bidirectional | Interactive debug shell |
| 3 | 9003 | EP_STDIO | up only | EP debug output (forwarded by the WP) |
| 4 | 9004 | EP_VFY | up only | EP verification output (forwarded by the WP) |

`nc` is handy for a quick manual look, for example `echo "boot" | nc -q1 localhost 9091`.
Its timeout behaviour varies across Linux versions, so scripts always use `harness/rtt.py`.

### HTTP

Once the WP is on WiFi, the harness calls its HTTP server directly.
No address is ever hardcoded: the `wifi` VFY command reports the WP's IP address, and the suites keep it in the shared context as `wp_ip`.

### The ap_proxy

The ap_proxy lets the PC talk to a umod4 that is in AP (Access Point) mode.
When powered, it continuously scans for the SSID of a umod4 in AP mode and connects using the default AP password (the same as the SSID).
It then acts as a proxy: the PC sends and receives the umod4's WiFi traffic through the ap_proxy's USB serial connection.
The provisioning suite uses it to supply the home-network credentials.

## The VFY protocol

The WP_VFY channel (RTT channel 1) is the main automation interface.
It is bidirectional and ASCII on both sides, so it can also be driven by hand.

**Command format (host to device):**

```text
<command> [arg1] [arg2] ...
```

**Response format (device to host):**

Every response is a single-line JSON object terminated with `\n`.
The top-level key is always the command name.
The value is an object with a `state` field plus any command-specific fields.

```json
{"wifi":{"state":"connected","ssid":"MyNet","ip":"192.168.1.42","rssi":-55}}
{"swd_test_flash":{"state":"ok"}}
{"swd_test_flash":{"state":"flasher_timeout"}}
```

### Naming convention

* **`xxx`** is a *status* command: it reads the current state of subsystem `xxx` and has no side effects.
* **`xxx_test_<name>`** is a *test* command: it performs an active operation on subsystem `xxx`.
  It has a result, and side effects are permitted.
* **`status`** is an *aggregate*: it calls all the status commands and returns their combined output.

The `state` field is always present.
For a status command it describes the current condition (`"connected"`, `"mounted"`, `"ap_mode"`).
For a test command it reports the outcome: `"ok"` or `"ready"` on success, or a failure reason (`"flasher_timeout"`, `"not_mounted"`).
The firmware only reports facts.
The harness always makes the pass or fail decision from `state`.

### A trap

`vfy_printf()` uses `BLOCK_IF_FIFO_FULL`.
Only call it in response to an explicit command, never unconditionally during boot or normal operation.
If no debugger is draining the buffer, the calling task blocks permanently.
Boot status belongs on WP_STDIO.

## How a run works

`runner.py` does the following, in order:

1. **Build** (`harness/build.py`).
   The runner runs the same top-level `ninja` build as F7 in VS Code, so the run always tests the sources in the working tree.
   When the build is already current this takes only a few seconds, because `ninja` knows what each artifact depends on and compiles nothing.
   A failed build is fatal.
   `--no-build` skips this step, for when you know the build is current: the run then says that the build was not checked.
2. **Pre-flight checks** (`harness/preflight.py`).
   They confirm that everything the run needs is present before anything is touched: the USB devices, the tools, the build artifacts, and the arguments needed for provisioning.
   A missing prerequisite aborts the run with a descriptive error.
   The one exception is a running `umod4_server.py`.
   The server answers the WP's UDP auto-discovery broadcast, which makes the WP save the server's address into its configuration and corrupts the test state.
   The runner therefore terminates it, with a logged warning.
3. **Flash and start OpenOCD.**
   The runner flashes the latest WP firmware, starts OpenOCD, and waits for RTT to come up.
   It then waits 5 seconds so the try-before-you-buy (TBYB) commit and the following cold boot finish before the first command is sent.
   Two suites, `test_provisioning` and `test_config_migration`, manage their own flashing and OpenOCD instead.
4. **Run the suites** in order, sharing one `context` dictionary between them.
5. **Report.**
   The runner writes a report under `build/test_reports/` and exits 0 if everything passed, or 1 if not.

On WSL2 the runner also attaches the debug probe to Linux (via usbipd) and detaches it on exit.

### The suite contract

A suite is a module in `tests/suites/` with a `run(ocd, results, context)` function.
It records outcomes through the `results` object: `passed`, `failed`, `warn`, `abort` and `fatal`.

* `failed` records a failure, and the suite carries on.
* `abort` ends this suite only.
  Later suites still run.
* `fatal` ends the whole run.
  The report is still written.
* An exception inside a suite is recorded as a failure, and the run continues.
* If a suite fails with a transient-looking error, the runner retries it once.
  The first attempt's failures are downgraded to warnings.

The `context` dictionary carries state between suites.
The runner seeds it with the command-line arguments, and suites add to it (for example the WP's IP address, `wp_ip`).
This is why suite order matters: a suite that needs `wp_ip` must run after the one that sets it.

After a fully successful run, the runner deletes the log and firmware files the run left on the WP's SD card.
After a failed run it leaves them, as evidence.

### Suite order and why

The order is fixed by the `SUITES` list in `runner.py`, and the reasons are written in the comment above it.
In outline: provisioning must come first so that every later suite tests known-good firmware.
The EP carries the ECU firmware, so the ECU is only on its latest firmware after the EP OTA suite has run.
The WP OTA suite goes last because the WP reboots at its end, which tears down the RTT session.

## Where the details live

| Need | Look at |
| --- | --- |
| What suites exist, in run order | `uv run tests/runner.py --list` (no hardware needed) |
| What one suite tests | The docstring at the top of its file in `tests/suites/` |
| Why the suites run in this order | The comment above `SUITES` in `tests/runner.py` |
| The VFY commands the firmware understands | The dispatch table in `WP/src/VfyTask.cpp` |
| What pre-flight checks exist | `tests/harness/preflight.py` |
| How to run the tests, set up USB, add a test | [README.md](README.md) |
| The latest results | `build/test_reports/latest.md` |

## Manual tests

These cover first-time setup, WiFi UX and browser behaviour that cannot be driven by RTT or HTTP alone.
A phone is required.

**Common preconditions:**

* The WP is built and flashed.
* The server (`tools/server/umod4_server.py`) is running on the PC for any test that uses a home network.

### T1: First-run flash of a completely erased Pico2W

**What it tests:** `flash_WP -e` succeeds on a blank device where no partition table has ever been written to flash.

```bash
tools/wp_enter_bootsel build/WpUsbBoot/WpUsbBoot
tools/flash_WP -e build/WP
```

**Pass:** the script exits 0, and within about 10 s the AP WiFi network `umod4_XXXX` is visible on a nearby phone or laptop.

**Fail indicators:**

* `wp_enter_bootsel` returns non-zero: check the debug probe is connected and the WP is powered.
* `wp_enter_bootsel` reports "RP2350 Boot device did not appear": check the USB cable from the WP to the PC.
* `flash_WP` reports "No RP2350 Boot device found": re-enter BOOTSEL and retry immediately.
* The SSID never appears: the firmware is running but the WiFi manager did not start, so check RTT.

**Automation:** the provisioning suite already does the flash steps and checks that the WP comes up in AP mode.

### T2: AP mode stability

**What it tests:** the WP stays in AP mode indefinitely when no WiFi credentials are configured.
A previous bug caused an AP, STA, AP thrash loop.

**Precondition:** the WP has a blank configuration: freshly erased (T1), or reset via the browser UI.

**Procedure:** power on the WP with a blank configuration and wait 60 s.
The SSID `umod4_XXXX` should remain visible throughout.

**Pass:** the SSID is stable for 60 s.
**Fail:** the SSID disappears and reappears, so the thrash loop is still present.

**Automation:** could poll the `wifi` VFY command for 60 s after a `wifi_reset` VFY command, which does not exist yet.

### T3: Captive portal

**What it tests:** connecting a phone to the AP triggers the "Sign in to network" notification, which opens `wifi_config.html` automatically.

**Precondition:** the WP is in AP mode (blank configuration).

**Procedure:** on the phone, connect to `umod4_XXXX` and wait up to 10 s.

**Pass (iOS/macOS):** a "Sign in to network" notification appears, and tapping it opens `wifi_config.html`.

**Pass (Android, Chrome/Edge):** the same notification and result.
The DuckDuckGo browser does not support captive portal detection.
That is a known browser limitation, not a firmware bug.

**Fail:** no notification after 30 s.
Navigate to `http://1.1.1.1` manually.
If that redirects to `wifi_config.html`, the DNS and HTTP redirect work but the OS probe did not fire.
Toggle WiFi off and on to reset the OS cache, and retry.

**Automation:** cannot be automated, because it needs a real phone OS.

### T4: Settings page, device name required

**What it tests:** `wifi_config.html` shows Device Identity at the top, pre-fills the current name, and blocks saving when the name field is empty.

**Procedure:**

1. Open `http://<device-ip>/wifi_config.html`.
2. Verify that "Device Identity" is the first section (red `*` on the label).
3. Verify that the Device Name field is pre-filled (for example `umod4_3BFF`).
4. Clear the field and tap "Save & Reboot": an error appears, and the device does NOT reboot.
5. Enter a name (for example `Robin's Tuono`) and tap "Save & Reboot": the device reboots, and the name persists.

**Automation:** fetching and parsing the HTML is automatable.
The JavaScript save validation needs a browser.

### T5: Status page, read-only

**What it tests:** `status.html` is a pure status page with no editable inputs, and a Settings link to `wifi_config.html`.

**Procedure:**

1. Open `http://<device-ip>/status.html`.
2. Verify that the ribbon says "Status" (not "Settings").
3. Verify that Device Name, MAC Address and Uptime show real values (not `--`).
4. Verify that Server Address shows the discovered server IP and port.
5. Verify that the grey "Settings" button links to `wifi_config.html`.
6. Verify that the red "Reboot Device" button is present, and that there is no "Save & Reboot" button.

**Automation:** fetch the page and check for no `<input` tags.
Not yet implemented.

### T6: UDP auto-discovery

**What it tests:** the WP broadcasts a UDP discovery packet and saves the server's IP and port automatically, with no compile-time or manually entered address.

**Precondition:** the WP has home WiFi credentials stored, and the flash configuration has no saved server address (freshly erased, or cleared with `flash_WP -e`).

**Procedure:**

1. Start `umod4_server.py` on the PC.
2. Power on the WP, which connects to the home WiFi.
3. Within 30 s a new device entry appears in the server GUI.
4. Open `status.html`: Server Address shows the PC's IP and port.
5. Reboot the WP: the address was saved, and the device connects immediately on the next boot without re-discovery.

**Pass:** the device appears in the server GUI without manual configuration, and the address persists across a reboot.

**Fail at step 3:** the device doesn't appear.
Check that the server is running and the WP is on the same subnet.
RTT shows "sendDiscoveryBroadcast" log lines if the broadcast is firing.

**Automation:** `harness/udp_checkin.py` already listens for the WP's discovery broadcast, and `test_ota_wp` uses it.
A full T6 test would add a check that the address is saved (an `/api/config` poll).
Not yet implemented.

### T7: Full first-time setup (end to end)

**What it tests:** the complete new-user experience, from a blank device through server discovery.

**Procedure:**

1. `tools/wp_enter_bootsel build/WpUsbBoot/WpUsbBoot`
2. `tools/flash_WP -e build/WP`
3. The phone connects to `umod4_XXXX`, and the captive portal appears (T3).
4. In `wifi_config.html`, enter a device name, the home WiFi SSID and the password, then Save & Reboot.
5. Reconnect the phone to the home WiFi.
6. The server GUI shows the new device with its name and IP.
7. `status.html` shows the name, MAC, server address and uptime.

**Pass:** the complete flow works without typing any IP address anywhere.

**Fail:** use T1 to T6 to isolate which step failed.

## Automation roadmap

| Test | Current status | What is needed to automate |
| --- | --- | --- |
| T1 first-run flash | Mostly automated by the provisioning suite | Nothing significant |
| T2 AP stability | Manual | A `wifi_reset` VFY command, then poll `wifi` for 60 s |
| T3 Captive portal | Manual, always | Needs a real phone OS: cannot be automated |
| T4 Settings page | Manual | HTTP fetch and HTML parse in a test suite |
| T5 Status page | Manual | HTTP fetch and HTML parse in a test suite |
| T6 UDP discovery | Partly (`udp_checkin.py` exists) | A check that the address is saved |
| T7 End to end | Manual for the phone steps | The flash and discovery steps are automatable |

Highest-value additions:

1. **A `wifi_reset` VFY command.**
   It would let tests put the WP into a blank-configuration state without reflashing, which enables T2 and parts of T7.
2. **A UDP discovery test.**
   It would build on `harness/udp_checkin.py` to verify T6 end to end.
3. **HTML structure checks.**
   A `urllib.request` fetch plus a regex covers the structural parts of T4 and T5 without a browser.
