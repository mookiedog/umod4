# Umod4 Test Harness

## To Do

- incorporate the new "health reporting" mechanisms into the test harness
    - will involve verifying that the WP, EP, and ECU report expected health observations **over time**.

## Prerequisites

- umod4 board
    - MicroUSB cable from PC to WP
    - Raspberry Pi Debug Probe connected to WP SWD header pins
    - GPS module and SD card should be installed
    - SPARE2 **must be floating** (grounding SPARE2 disables WP control of EP via SWD)
- Test System
    - Minimum: a bare umod4 board on a bench (no ECU), although EP testing will be limited, and ECU tests will be non-existent
    - Full testing requires umod4 installed in a powered ECU (ECU does not need to be installed on a bike though)

- ap_proxy device (Pico W programmed with ap_proxy firmware) connected via USB cable
- OpenOCD installed at `/usr/local/bin/openocd`
- Python 3.10+

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

A UAC prompt will appear on the Windows desktop — click Yes. Both the debug probe
and ap_proxy must be plugged in when you run this. Re-run it if you replace either
device with a new physical unit.

## Running the Tests

### Step 1: Build Umod4 System

Press **F7** in VS Code to build **all** umod4 firmware.

### Step 2: Run the Test Suite

A full test run will automatically flash the latest WP/EP/ECU firmware as part of the test suite sequence.
The difference between a full run and a partial run is whether the WP's device configuration is erased:

**Full run (with provisioning)** — erases all WP flash including the config partition,
flashes the latest firmware, then provisions WiFi credentials and device name from
scratch.
This would be the typical way to run the test suite, testing everything.

```bash
build/.venv/bin/python3 tests/runner.py --ssid <network> --password <pw> --device-name <name>
```

The `--ssid` and `--password` arguments can also be supplied via environment variables
`UMOD4_WIFI_SSID` and `UMOD4_WIFI_PASSWORD`, which is convenient when running the
suite repeatedly:

```bash
export UMOD4_WIFI_SSID="MyNetwork"
export UMOD4_WIFI_PASSWORD="secret"
build/.venv/bin/python3 tests/runner.py
```

**Partial run (skip provisioning)** — flashes the latest WP firmware but leaves the
WP config partition untouched.
The board's existing WiFi credentials and device name are
preserved.
A developer might use this mode to save time during test suite development.

```bash
build/.venv/bin/python3 tests/runner.py test_basic test_ep_swd test_wifi test_ota_ep test_ota_wp
```

To run a single suite:

```bash
build/.venv/bin/python3 tests/runner.py test_basic
```

### Expected Output

```
umod4 test harness
------------------
Starting OpenOCD...
RTT ready.

[suites.test_basic]
  PASS  ping
  PASS  version  {"version":"PASS","bt":"May  1 2026 16:39:26"}
  PASS  status   {"status":"PASS","uptime_ms":5420,"heap_remaining":...,"lfs_mounted":1}
  PASS  lfs_test

OK  4/4 passed
...
```

## USB Device Management (WSL2)

On WSL2, USB devices must be forwarded from Windows to the Linux subsystem by
usbipd before the harness can use them.  Devices go through two distinct states:

| usbipd state | Meaning |
| --- | --- |
| **Not shared** | Device is not bound — usbipd will not forward it. Run `tools/setup_usb_wsl` to bind it. Binding survives unplug/replug and reboots. |
| **Shared** | Device is bound and ready to be forwarded, but is currently held by Windows (not in WSL). |
| **Attached** | Device has been handed off to WSL by usbipd, but **Linux USB enumeration may still be in progress**. |

**What the runner does automatically** — you do not need to run `usbipd attach` by hand:

- At startup the runner calls `ensure_attached("2e8a:000c")` to forward the CMSIS-DAP
  debug probe to WSL.
- The provisioning suite calls `ensure_attached` for the ap_proxy when it needs it.
- `ensure_attached` does not just call `usbipd attach` and return.  After usbipd reports
  "Attached", it polls `/sys/bus/usb/devices/` until the device is actually enumerated on
  the Linux side and visible to libusb.  This is necessary because usbipd "Attached" only
  means the Windows-side handoff is complete — Linux USB enumeration takes an unpredictable
  extra moment, and OpenOCD will fail to find the device if it runs before that completes.

**What you must do once** (one-time setup, not repeated per run):

- Run `tools/setup_usb_wsl` as described in the Prerequisites section to bind both devices.
  Without binding, `ensure_attached` raises a fatal error before any tests run.

**Symptoms of a binding problem vs. an enumeration problem:**

- `FATAL cmsis_dap_usb ... not found` or `state: Not shared` → device is not bound.
  Run `tools/setup_usb_wsl`.
- `FATAL prov_enter_bootsel ... unable to find a matching CMSIS-DAP device` → the device
  was attached but Linux did not enumerate it in time.  This should no longer happen after
  the enumeration-wait fix in `ensure_attached`, but if it recurs it indicates usbipd or
  the Linux USB stack is unusually slow — check `dmesg` for USB errors.

## How It Works

The harness communicates with the WP firmware over RTT (Real Time Transfer) via SWD:

- The runner launches its own OpenOCD subprocess and waits for it to find the RTT control block in WP RAM.
- Each RTT channel is exposed as a TCP port (`9000 + channel number`).
- Test suites send ASCII commands to the **WP_VFY** channel (port 9001) and read back single-line JSON responses (`{"cmd":"PASS",...}` or `{"cmd":"FAIL","reason":"...",...}`).
- OpenOCD is shut down cleanly when the runner exits.

## RTT Channel Map

| Channel | Port | Name     | Direction    | Purpose                        |
|---------|------|----------|--------------|--------------------------------|
| 0       | 9000 | WP_STDIO | up only      | WP debug printf output         |
| 1       | 9001 | WP_VFY   | bidirectional| Test automation commands/results|
| 2       | 9002 | WP_SHELL | bidirectional| Interactive debug shell        |
| 3       | 9003 | EP_STDIO | up only      | EP debug output (forwarded)    |
| 4       | 9004 | EP_VFY   | up only      | EP verification (forwarded)    |

## Adding New Tests

1. Add a command handler in `WP/src/VfyTask.cpp`
2. Add the test to an existing suite in `tests/suites/` or create a new suite file
3. Register new suites in the `SUITES` list in `tests/runner.py`

## Changing the ap_proxy VID:PID

The ap_proxy VID:PID has a **single source of truth**: `tools/ap_proxy/usb_ids.txt`.
Edit `AP_PROXY_VID` and `AP_PROXY_PID` there, then verify the following:

| Consumer | How it gets the value | Action needed |
| --- | --- | --- |
| `tools/ap_proxy/CMakeLists.txt` | reads `usb_ids.txt` at CMake configure time | **none** — rebuild ap_proxy |
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
