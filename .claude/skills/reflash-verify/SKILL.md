---
name: reflash-verify
description: Flash new WP or EP firmware onto a umod4 board and prove the new build is actually the one running. Use when the user asks to flash, reflash, load or update WP or EP firmware, to debug on the EP or WP, or to confirm which build is running after a flash.
---

# Reflash and verify

The rule from AGENTS.md section 5: after any reflash, confirm the new build is actually running before trusting a result.
A flash command that exits cleanly proves nothing about what is running.

## Pick the method

| Goal | Method |
|------|--------|
| Update the WP during development | `tools/wp_enter_bootsel`, then `tools/flash_WP [-e] ~/projects/umod4/build/WP`, or the VS Code task "Wp: Flash and Run (Force BOOTSEL via SWD)". `-e` erases flash first |
| Update the WP on a board that is already on WiFi | Server GUI, Reflash WP (OTA) |
| Update the EP | Server GUI, Reflash EP. Or `tools/server/run_server.sh --ip <device-ip> --flash-ep ~/projects/umod4/build/EP/EP.uf2` |
| Debug the WP | VS Code: attach to running firmware, or the partition-aware loader (see below) |
| Debug the EP | VS Code target "*** EP: RP2040 Launch CMSIS-DAP", with SPARE2 grounded (see below) |
| Blank board, first install | The three-step BOOTSEL drag-and-drop (WP_erase, WP_partition, WP). Follow the install docs, do not improvise |
| Test runs | The harness flashes the WP itself (see the hardware-test-run skill) |

How these work:

- **EP flashing is done by the WP over SWD.** The WP must be running, on the network, and have its SD card inserted, because it stages the EP image there.
- **`flash_WP` needs the WP in BOOTSEL.** On WSL2 it does the usbipd attach itself. USB trouble is covered in the hardware-test-run skill.
- **Build first:** `cd ~/projects/umod4/build && ninja`. Then confirm `build/WP/WP.uf2` or `build/EP/EP.uf2` has a fresh timestamp.

## Traps

- **Never let a debugger write a WP image straight to its run address.**
  The WP flash holds two image slots plus partition data, and the bootloader remaps a slot to the run address.
  A normal debugger load lands at the run address and destroys the partitions.
  Use "attach to running firmware", or the partition-aware loader (it erases slot 1 and loads slot 0).
- **Debugging the EP conflicts with the WP.**
  Both would drive the EP's SWD pins.
  Power off, ground SPARE2, attach the debugger, then power on.
  When finished: detach, let SPARE2 float, reboot the WP.
  For test-harness runs SPARE2 must be floating.
- **Changing EP code that affects bus timing triggers the ecu-ep-safety rules** (scope verification, comment updates).
  A successful flash says nothing about timing.

## Verify the new build is running

1. **Prove the artifact is fresh.** `ninja` should have rebuilt, and the `.uf2` timestamp should be newer than your last source edit.
2. **WP: read the boot info over RTT.**
   The WP's verification channel reports `boot` with `slot`, `target` and `built`.
   The test harness's `test_basic` (`version` command) prints the same build time.
   After an OTA, compare `slot` and `target`.
3. **Know what `built` actually is.**
   In `WP/src/VfyTask.cpp` it is `__DATE__ __TIME__`, the compile time of that one file.
   If a build did not recompile `VfyTask.cpp`, `built` will look old even though the new code is running.
   This is my reading of the code, not something I have tested on hardware.
   For a result that matters, touch `WP/src/VfyTask.cpp` before building so the timestamp changes with every build, then confirm it changed.
4. **EP: read its build metadata.**
   The EP emits `LOGID_EP_BUILD_META` (git identity plus a timestamp) at boot, and it ends up in the log.
   The WP forwards EP RTT output as well.
   The git identity comes from `git describe` in the top-level CMake. My reading is that it runs when CMake CONFIGURES, so it can be stale until a reconfigure. Unverified.
5. **Behavior check.** After the identity check, do one observable thing the new build changes, so you are not relying only on a version string.
6. **Report plainly:** which build you expected, which you saw, how you saw it, and what you did not check.

## When it fails

- Explain why before retrying anything.
- The SwdReflash EP path has a known intermittent failure: `flash_timeout` with `last_status:-1`, roughly half the runs in earlier testing, cause not understood.
  Do not describe it as understood.
- After two failures of the same approach, stop and write the four-part summary from CLAUDE.md: what was tried, what is known versus guessed, the open question, and whether to continue.
- Ask the user before anything physical: pressing BOOTSEL, grounding SPARE2, unplugging.

## Keeping this skill current

At the end of a session that used this skill, if you hit a situation not covered here, propose a specific edit and wait for approval.
Never edit this file unasked.
