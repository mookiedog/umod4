---
name: change-tool-version
description: Change the version of a tool or dependency the umod4 build uses (Pico SDK, FreeRTOS-Kernel, ARM toolchain, 68HC11 binutils, OpenOCD, Python) and keep every place that names it in step. Use when the user wants to upgrade or pin one of these, when a new SDK or toolchain release comes out, or when a version in BUILDING.md disagrees with the machine.
---

# Changing a tool or dependency version

Most places that name a version are documentation, and only two versions are real settings (the SDK and Python).
So a version change is a checklist, not a mechanism: miss one place and a document quietly disagrees with the build.
The table below came from searching the repository for each version string on 2026-11-21.
Search again before relying on it (`git grep -n -I -e '<version>'`), because places get added.

## Before you start

1. Say which component and which version, and wait for a yes.
2. Check what is installed with `scripts/check_build_env -v`.
   It reports the installed versions and flags a checkout that is not on the expected tag.
3. Some changes touch dangerous areas (AGENTS.md section 4): see "What to re-test", below.
4. A version change means deleting `build` (the comment next to `PICO_SDK_VERSION` requires it for an SDK change).
   First check that every folder in `build/dist` is already saved in `~/umod4-releases` (see the release skill), or move `build` aside instead of deleting it.

## Where each version is named

BUILDING.md has a "Versions known to work" table, install commands, verify commands and prose for every component.
Search it for the old version rather than trusting line numbers.

| Component | The real setting | Also update |
|-----------|------------------|-------------|
| **Pico SDK** | `PICO_SDK_VERSION` in the top-level `CMakeLists.txt` | BUILDING.md (table, clone and verify commands, directory diagrams); the directory diagram in the `CMakeLists.txt` comment; CLAUDE.md (several lines); `scripts/check_build_env` needs nothing, it reads the setting |
| **pico-examples** | none: derived from the SDK version | the clone and verify commands in BUILDING.md follow the SDK change |
| **picotool** | none: the superbuild builds the tag named by the SDK version | after an SDK change, run `ninja picotool` in `build`: an older picotool in `~/.local` makes the SDK stop with "Incompatible picotool installation found" |
| **FreeRTOS-Kernel** | `FREERTOS_KERNEL_VERSION` in the top-level `CMakeLists.txt` (CMake and `scripts/check_build_env` both read it) | BUILDING.md (table, clone and verify commands, directory diagram); CLAUDE.md |
| **ARM toolchain** | none: the build uses the newest directory under `/opt/arm/arm-none-eabi`, unless `ARM_NONE_EABI_VERSION` is set in `cmake/toolchains/arm-none-eabi.cmake` | BUILDING.md; CLAUDE.md (two lines) |
| **68HC11 binutils** | none | BUILDING.md only |
| **OpenOCD** | none | BUILDING.md; a comment in `CMakeLists.txt` |
| **Python** | `requires-python` in `pyproject.toml`, then `uv lock` (the comment above that line lists the steps) | BUILDING.md; CLAUDE.md; RELEASING.md; `tests/README.md`; `tools/bundle/README.txt` |

Notes:

* Comments in `SwdReflash/src/main.c` mention SDK 2.3.0 as history.
  Do not update them.
* The harness finds `nm` through the `build/arm_toolchain_latest` symlink that CMake creates, so an ARM toolchain change needs no harness edit.
  That symlink is made at configure time: after installing a new toolchain, reconfigure (VS Code: "CMake: Delete Cache and Reconfigure").
* The version-change search does not catch other spellings (such as `2_3_0`) or version numbers computed elsewhere.
  Say so if you rely on it.

## What to re-test

Do not call a version change safe for the bike.
State what was and was not tested.

* **Any change:** `scripts/check_build_env`, then a clean rebuild (`rm -rf build`, reconfigure, `ninja`), then the full test runner (`uv run tests/runner.py`; the hardware-test-run skill has the details).
* **ARM toolchain or SDK:** these can change the machine code of the EP and the WP.
  The EP's EPROM emulation has hard timing limits, so follow the ecu-ep-safety skill.
  I expect a new compiler to leave the hand-written assembly in `epromEmulator.S` alone, but I have not verified that, and the surrounding C code and the PIO code could differ.
  Comparing `objdump -d` output before and after is a cheap first check; the scope procedure in the ecu-ep-safety skill is the real one.
  Compare the builds before and after instead of trusting a clean build:
  * Before the change, save `EP/EP`, `WP/WP.elf`, `ecu/UM4.bin`, the link maps (`EP/EP.map`, `WP/WP.elf.map`) and `objdump -d` of both ELFs.
  * After the rebuild, compare the set of `.time_critical.*` sections in each map (a section that disappears means that function was inlined into flash code), compare the disassembly function by function with addresses stripped, and confirm `epromTask` has the same address and the same bytes.
  * Expect the ECU image to differ only in its embedded build-info string (git description and build time), and expect SDK-internal functions to differ when the SDK changes.
  * This is how the SDK 2.3.1 change was found: it removed `__noinline` from `__time_critical_func`, and `logMapblob` was inlined into `main` and moved from RAM to flash.
    A plain objdump comparison with addresses left in shows a hundred false differences, so strip addresses and `@` comments first, and check the comparison tool by confirming it finds instructions in a function you know changed.
* **FreeRTOS-Kernel:** BUILDING.md says not to move to a newer release without re-testing a full WP build on real hardware.
  It is pinned on purpose, and the `RP2350_ARM_NTZ` port must still exist in the new release.
* **68HC11 binutils:** rebuild the ECU image and compare `build/ecu/UM4.bin` byte for byte with the one from the old toolchain.
  Identical output is strong evidence the change is safe; a difference needs investigation before any bench test.
* **OpenOCD:** run the harness, since it drives OpenOCD for every suite.
* **Python:** BUILDING.md and `pyproject.toml` say to rebuild everything and run the test suite.

## Finish

1. `git grep` for the old version and read every remaining hit.
   Each should be intentional (history) or fixed.
2. Run `scripts/check_build_env` once more.
3. Commit with the commit skill.
   Put the version change and all its documentation in one commit.

## Keeping this skill current

At the end of a session that used this skill, if you hit a situation not covered here, propose a specific edit and wait for approval.
Never edit this file unasked.
