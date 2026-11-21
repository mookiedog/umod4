# Umod4 Test Harness

This document explains how to run the automated test harness.
[TEST_PLAN.md](TEST_PLAN.md) explains how it works and why it is built the way it is.

## Prerequisites

* umod4 board
  * MicroUSB cable from PC to WP
  * Raspberry Pi Debug Probe connected to WP SWD header pins
  * GPS module and SD card should be installed
  * SPARE2 **must be floating** (grounding SPARE2 disables WP control of EP via SWD)
* Test System
  * Minimum: a bare umod4 board on a bench (no ECU), although EP testing will be limited, and ECU tests will be non-existent
  * Full testing requires umod4 installed in a powered ECU (ECU does not need to be installed on a bike though)
  * The ECU suite checks the ECU's sensor readings, so the ECU needs fake sensors wired to it (see [Fake sensors for the ECU](#fake-sensors-for-the-ecu))
* ap_proxy device (Pico W programmed with ap_proxy firmware) connected via USB cable
* OpenOCD installed at `/usr/local/bin/openocd`
* Python 3.12, installed and managed by uv (see [BUILDING.md](../BUILDING.md)): there is nothing to install by hand

### Fake sensors for the ECU

A bench ECU has no engine, so the ECU test suite (`test_ecu`) expects the ECU's coolant temperature, air temperature and throttle position inputs to be fed by fixed components: two resistors and a resistor divider with an adjustment pot.
[BENCH_TESTING.md](../BENCH_TESTING.md#external-sensors) lists the component values, the temperatures they simulate, and the circuit.
The resistor values the suite checks against are constants at the top of `tests/suites/test_ecu.py`: if your bench uses different parts, change them there.
The expected ECU error register can be overridden with `--l000c` if your bench has a different set of sensors connected.

### One-time Linux/WSL setup

Add your user to the `dialout` group so Python can open serial ports (`/dev/ttyACM*`):

```bash
sudo usermod -aG dialout $USER
```

Then **log out and back in** (or open a new WSL terminal) for the group membership to take effect.

### One-time Windows/WSL2 USB setup

Run the USB binding script so usbipd can share Pico-family devices with WSL:

```bash
tools/setup_usb_wsl
```

A UAC prompt will appear on the Windows desktop: click Yes.
Both the debug probe and ap_proxy must be plugged in when you run this.
Re-run it if you replace either device with a new physical unit.

## Running the Tests

### Step 1: Build Umod4 System

The runner builds the firmware itself before it tests, so there is nothing to do here.
If you want to build first anyway, press **F7** in VS Code.
A build that is already current costs the runner only a few seconds.
To skip the runner's build step, add `--no-build`: the run then tests the artifacts exactly as they are.

### Step 2: Run the Test Suite

A full test run will automatically flash the latest WP/EP/ECU firmware as part of the test suite sequence.
The difference between a full run and a partial run is whether the WP's device configuration is erased.

**Full run (with provisioning)**: erases all WP flash including the config partition, flashes the latest firmware, then provisions WiFi credentials and device name from scratch.
This would be the typical way to run the test suite, testing everything.

```bash
uv run tests/runner.py --sta-ssid <network> --sta-password <pw> --device-name <name>
```

The `--sta-ssid` and `--sta-password` arguments can also be supplied via environment variables `UMOD4_STA_SSID` and `UMOD4_STA_PASSWORD`.
This is convenient when running the suite repeatedly.
`UMOD4_DEVICE_NAME`, `UMOD4_AP_SSID` and `UMOD4_AP_PASSWORD` work the same way:

```bash
export UMOD4_STA_SSID="MyNetwork"
export UMOD4_STA_PASSWORD="secret"
uv run tests/runner.py
```

**Partial run (skip provisioning)**: flashes the latest WP firmware but leaves the WP config partition untouched.
The board's existing WiFi credentials and device name are preserved.
A developer might use this mode to save time during test suite development.

```bash
uv run tests/runner.py test_basic test_ep_swd test_wifi test_ota_ep test_ota_wp
```

To run a single suite:

```bash
uv run tests/runner.py test_basic
```

To see which suites exist, and what each one does:

```bash
uv run tests/runner.py --list
```

This needs no hardware.
The list is the one the runner itself uses, so it is always current.

### Expected Output

A run prints the pre-flight checks, then each suite in turn with one line per test.
An abbreviated successful run looks like this:

```text
[harness.build]
  PASS  ninja  build is up to date (3s)
[harness.preflight]
  PASS  server_kill
  PASS  ap_proxy_usb  ap_proxy (1209:0001) present (state: Shared)
  PASS  cmsis_dap_usb  CMSIS-DAP (2e8a:000c) present (state: Shared)
  ...
Entering WP BOOTSEL mode via SWD...
Flashing WP from /home/you/projects/umod4/build/WP ...
Flash complete.

Starting OpenOCD...
RTT ready.

[suites.test_basic]
  PASS  boot  slot=1 built=Nov 18 2026 01:00:04
  PASS  heap  remaining=122880 free=7032
  PASS  sd  size_mb=60906
  PASS  filesystem_test_rw
  PASS  logstore_fsck
  heap: remaining=122880 free=7032

...

OK  71/71 passed (0 retries)
Report: /home/you/projects/umod4/build/test_reports/run_20261119_072147.md
```

Each test line starts with `PASS`, `FAIL`, `WARN`, `ABORT` or `FATAL`.
The last line before the report path is `OK` if everything passed and `FAILED` if not, and the exit code is 0 or 1 to match.
The report file has the same results, plus the WP's own debug output for each suite.
The numbers and values above are only an example.

### Nightly runs

`tools/setup_nightly_tests` registers a Windows Scheduled Task that runs the suite every night.
See [Nightly Test Job](../Tools.md#nightly-test-job) in Tools.md.

## USB Device Management (WSL2)

On WSL2, USB devices must be forwarded from Windows to the Linux subsystem by usbipd before the harness can use them.
Devices go through two distinct states:

| usbipd state | Meaning |
| --- | --- |
| **Not shared** | Device is not bound: usbipd will not forward it. Run `tools/setup_usb_wsl` to bind it. Binding survives unplug/replug and reboots. |
| **Shared** | Device is bound and ready to be forwarded, but is currently held by Windows (not in WSL). |
| **Attached** | Device has been handed off to WSL by usbipd, but **Linux USB enumeration may still be in progress**. |

**What the runner does automatically.**
You do not need to run `usbipd attach` by hand:

* At startup the runner calls `ensure_attached("2e8a:000c")` to forward the CMSIS-DAP debug probe to WSL.
* The provisioning suite calls `ensure_attached` for the ap_proxy when it needs it.
* `ensure_attached` does not just call `usbipd attach` and return.
  After usbipd reports "Attached", it polls `/sys/bus/usb/devices/` until the device is actually enumerated on the Linux side and visible to libusb.
  This is necessary because usbipd "Attached" only means the Windows-side handoff is complete.
  Linux USB enumeration takes an unpredictable extra moment, and OpenOCD will fail to find the device if it runs before that completes.

**What you must do once** (one-time setup, not repeated per run):

* Run `tools/setup_usb_wsl` as described in the Prerequisites section to bind both devices.
  Without binding, `ensure_attached` raises a fatal error before any tests run.

**Symptoms of a binding problem vs. an enumeration problem:**

* `FATAL cmsis_dap_usb ... not found` or `state: Not shared` means the device is not bound.
  Run `tools/setup_usb_wsl`.
* `FATAL prov_enter_bootsel ... unable to find a matching CMSIS-DAP device` means the device was attached but Linux did not enumerate it in time.
  This should no longer happen after the enumeration-wait fix in `ensure_attached`.
  If it recurs, it indicates usbipd or the Linux USB stack is unusually slow: check `dmesg` for USB errors.

## How It Works

How the harness reaches the board, the RTT channel map, the VFY protocol and how a run is structured are in [TEST_PLAN.md](TEST_PLAN.md).

## Adding New Tests

1. Add a command handler in `WP/src/VfyTask.cpp`
2. Add the test to an existing suite in `tests/suites/` or create a new suite file
3. Register new suites in the `SUITES` list in `tests/runner.py`, then check that `uv run tests/runner.py --list` shows it

## Changing the ap_proxy VID:PID

The ap_proxy VID:PID has a **single source of truth**: `tools/ap_proxy/usb_ids.txt`.
Edit `AP_PROXY_VID` and `AP_PROXY_PID` there, then verify the following:

| Consumer | How it gets the value | Action needed |
| --- | --- | --- |
| `tools/ap_proxy/CMakeLists.txt` | reads `usb_ids.txt` at CMake configure time | **none**: rebuild ap_proxy |
| `tests/harness/usb_ids.py` | reads `usb_ids.txt` at import time | **none** |
| `tests/harness/ap_proxy.py` | imports from `harness.usb_ids` | **none** |
| `tests/harness/preflight.py` | imports from `harness.usb_ids` | **none** |
| `tests/suites/test_provisioning.py` | imports from `harness.usb_ids` | **none** |
| `tools/set_credentials` | reads `usb_ids.txt` at startup | **none** |
| `tools/flash_ap_proxy` | reads `usb_ids.txt` at startup | **none** |
| `tools/setup_usb_wsl.ps1` | reads `usb_ids.txt` (copied to TEMP by launcher) | **none** |
| `/etc/udev/rules.d/99-proxy-pico.rules` | hardcoded | **update manually**, then reload udev rules |
| `tools/ap_proxy/SETUP.md` | documentation | **update manually** |

After editing `usb_ids.txt`, rebuild the ap_proxy firmware and re-flash it:

```bash
cd ~/projects/umod4/build && ninja ap_proxy
tools/flash_ap_proxy
```
