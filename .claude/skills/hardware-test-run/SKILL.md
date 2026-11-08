---
name: hardware-test-run
description: Run the umod4 hardware-in-the-loop test harness (tests/runner.py) and interpret the results. Use when the user asks to run the tests, asks how to run the tests or the test harness, run the test suite, run a specific suite such as test_basic or test_wifi, check preflight, or when a test run fails with USB, probe, usbipd, OpenOCD or RTT errors.
---

# Running the umod4 hardware test harness

The harness needs real hardware.
Do not run it unless the user has said the board is connected.
Full details live in `tests/README.md`; this skill is the order of operations and the known traps.

If the user only asks HOW to run the tests, explain the options in section 2, and mention that preflight runs automatically at the start.
Do not start a run until they ask for one.

## 1. Before running anything

1. Ask the user to confirm the board is connected and in the right state:
   - umod4 board powered, Debug Probe on the WP SWD pins, ap_proxy plugged in, GPS and SD card installed.
   - **SPARE2 must be floating.** Grounding it (needed for EP debugging) disables the WP's SWD control of the EP and breaks the EP suites.
2. Firmware must be built first: `cd ~/projects/umod4/build && ninja`.
   The runner aborts on stale build artifacts; `--allow-stale` only downgrades that to a warning, so do not use it casually.
3. Preflight runs automatically at the start of every run (unless `--skip-preflight` is passed), so a separate preflight run is optional.
   If the user wants to check the setup without starting a run, offer `uv run tests/runner.py --preflight-only`.
   It checks USB devices (probe `2e8a:000c`, ap_proxy `1209:0001`), tools and build artifacts.
   If preflight fails, fix that before retrying (section 4).

## 2. Choose the run type

| Goal | Command |
|------|---------|
| Everything, including provisioning | `uv run tests/runner.py --sta-ssid <ssid> --sta-password <pw> --device-name <name>` |
| Specific suites, keep existing config | `uv run tests/runner.py test_basic test_wifi` |
| Skip the WP reflash entirely | add `--no-flash` |

- **A full run with no suite names is destructive:** provisioning erases the WP flash, including the config partition (WiFi credentials, device name).
  Say this to the user and get a yes before starting it.
- Never invent WiFi credentials or a device name. Ask, or use the `UMOD4_STA_SSID`, `UMOD4_STA_PASSWORD` and `UMOD4_DEVICE_NAME` environment variables if the user has set them.
- Suites are in `tests/suites/`. `test_provisioning` and `test_config_migration` manage their own flashing.
- Clean the SD card filesystem BEFORE a run, not after.
  Leftover logs from failed runs are useful evidence, but accumulated logs once made LittleFS take over an hour to allocate at boot (see memory `project_nightly_lfs_cleanup`).

## 3. After the run

- Reports: `build/test_reports/latest.md` (and a timestamped `run_*.md`).
  Read it and summarize pass/fail counts and each failure's reason. Do not paste the whole report.
- Report outcomes faithfully. If a suite failed or was skipped, say so with the output.
- A failure is not automatically a firmware bug. Check section 4 first.
- Reproduce a failure before fixing it. Rerun only the failing suite.
- Hard stop: two failed attempts at the same approach means stop and write the four-part summary from CLAUDE.md (tried / known vs guessed / open question / continue or stop).
- The user asked to be reminded: before any commit, ask whether the test runner has been run.

## 4. Known USB and environment traps (WSL2)

Probe and ap_proxy are forwarded from Windows with usbipd. The runner attaches them itself; the one-time bind is `tools/setup_usb_wsl` (UAC prompt, the user must click Yes).

- `FATAL ... not found` / `state: Not shared`: the device was never bound. Run `tools/setup_usb_wsl`.
- "probe appears busy" from preflight is often really "no matching CMSIS-DAP device".
  Typical cause: the usbipd service was restarted, which detaches everything from WSL.
  Fix, in a Windows shell: `usbipd.exe attach --hardware-id 2e8a:000c --wsl` and the same for `1209:0001`.
- Attach fails with `Device in error state` on the RP2350 Boot device (`2e8a:000f`):
  the device was probably bound with `--force`.
  In an Administrator PowerShell with the WP in BOOTSEL: `usbipd unbind --busid X`, then `usbipd bind --busid X` **without** `--force`.
  Different USB ports and `Restart-Service usbipd` did not help. This is strongly suggested but not proven.
- `usbipd: warning: The device appears to be used by Windows`: Windows Search Indexer is holding the BOOTSEL drive.
  The fix is to exclude removable drives from Windows Search indexing, not `bind --force`.
- `VBoxUSBMon` in the Windows event log is usbipd-win's own driver. Never stop it.
- Serial port permission errors: the user must be in the `dialout` group (one-time `sudo usermod -aG dialout $USER`, then a new terminal). You cannot type the sudo password; have the user run it.

If none of these fit, say so plainly. Do not guess at hardware causes.

## 5. Keeping this skill current

At the end of a session that used this skill, if you hit a failure, workaround or fact that is not covered here, propose a specific edit and wait for approval.
Never edit this file unasked.
If the fact belongs in `tests/README.md` instead, say so and point to it from here.
