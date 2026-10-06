# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Claude: Please recognize when you are flailing during a development session.
It does NO GOOD to just flail and see what sticks to the wall.
Flailing would include things like:

* Making overly confident predictions after multiple failures
* Trying the same thing with slight variations
* trying random things "just in case"
* Attempting more than 2 fixes without understanding the root cause

If the user converses with all caps or is swearing, STOP AND LISTEN TO THEM.
Do NOT keep blindly charging ahead.

## Honesty Under Uncertainty

Before attempting any hardware/firmware approach you are not certain about:

- State your confidence level explicitly ("I know this from docs/disassembly" vs "I believe this but am not sure")
- State what you DON'T know that could cause it to fail

After a failure:

- Explain WHY it failed before proposing anything new
- If you cannot explain the failure, say so explicitly: "I don't know why that didn't work"
- Do NOT propose a variant of the same approach if you can't explain the previous failure

Hard stop rule: After 2 failures on the same approach, stop and write a summary:

1. What you tried
2. What you know for certain vs. what you guessed
3. What the open question is
4. Whether you recommend continuing or stopping

When the user asks "what is going on?" or "are you sure?", treat this as a signal to stop and give an honest assessment — not to defend the current approach and propose the next attempt.

## Other Interactions

- if the user ends a prompt using a phrase or sentence like "Discuss." it means do not rush ahead and start editing, it means that you and the user need to discuss the situation before moving forward with a solution.

## Project Overview

Umod4 is a data logging system for Aprilia Gen 1 motorcycle fuel injection ECUs. It replaces the ECU's EPROM with a sophisticated circuit board containing two ARM processors (EP and WP) that emulate the EPROM, log ECU data streams, correlate with GPS data, and provide future wireless capabilities.

## Build System Architecture

### CMake Superbuild Structure

This project uses CMake's **ExternalProject_Add** superbuild pattern because the Raspberry Pi Pico SDK can only be configured once per CMake namespace. Since the project requires two different board configurations (EP and WP), each must have its own namespace.

**Critical Build Constraints:**
- Never try to build this as a single unified CMake project
- Each external project (tools, ecu, eprom_lib, EP, WP) has its own build namespace.
- Build dependencies: `EP` depends on `tools`, `ecu`, `eprom_lib`; `WP` depends on `tools`, `EP`

### Building the Project

```bash
# Initial configuration (or after major changes)
# In VS Code: F1 -> "CMake: Delete Cache and Reconfigure"

# Build everything
# In VS Code: F7
# Or from command line:
cd ~/projects/umod4/build
ninja

# Build individual components (from their build directories)
cd ~/projects/umod4/build/EP && ninja
cd ~/projects/umod4/build/WP && ninja
cd ~/projects/umod4/build/ecu && ninja
```

**Python Environment:**
The Python environment is managed by uv. `pyproject.toml` lists the packages (in dependency groups) and `uv.lock` pins their versions. The build runs `uv sync --locked`, which creates the virtual environment at `<repo>/.venv`. Run tools with `uv run <script>` from anywhere inside the repo; there is no venv to activate. The project is pinned to Python 3.12 (`requires-python = "==3.12.*"`; see the comment above that line in `pyproject.toml` for why and how to change it). A truly clean state means deleting `.venv` as well as `build/`. Generated Python modules (`Logsyms.py`, `LogMeta.py`) are written to `build/python_packages/`.

### Build Output Locations

- ECU firmware: `build/ecu/`
- EP firmware: `build/EP/EP.uf2`, `build/EP/EP.elf`
- WP firmware: `build/WP/WP.uf2`, `build/WP/WP.elf`
- Tools: `build/tools/` (installed to `~/.local/bin`)
- EPROM library: `build/eprom_lib/`

## Component Architecture

### ECU (Engine Control Unit)
- **Processor:** Motorola 68HC11G5 (512 bytes RAM)
- **Toolchain:** Custom-built m68hc11-elf binutils (assembler, linker, objcopy)
- **Language:** Pure assembly (`ecu/src/UM4.S`)
- **Purpose:** Custom data-logging firmware based on stock 549USA/RP58 EPROM
- **Log Events:** Emits timestamped events for crank position, ignition, injection, sensors, etc.

**Key Files:**
- `ecu/src/UM4.S` - Main ECU assembly code
- `lib/inc/log_ids.h` - log event ID definitions (shared by the ECU, EP and WP)

### EP (EPROM Processor)
- **Processor:** RP2040 (Cortex-M0+)
- **SDK:** Raspberry Pi Pico SDK 2.3.0 (configurable in top-level CMakeLists.txt)
- **Toolchain:** arm-none-eabi-gcc 15.2.rel1 (at `/opt/arm/arm-none-eabi/15.2.rel1`)
- **Source:** `EP/src/`

**Dual-Core Architecture:**
- **Core 1:** EPROM emulation - Responds to HC11 read/write requests at 2MHz bus speed using 27 GPIO pins
- **Core 0:** Flash management, UART communication with WP, image loading from SPI flash

**Key Capabilities:**
- Emulates 32KB EPROM with regions that can act as RAM (expands ECU's 512 bytes)
- Loads and combines multiple EPROM images from 16MB SPI flash partition
- Forwards ECU data stream to WP via UART at 2.5 megabaud
- Logs all HC11 accesses to circular buffer (8,192 entries × 4 bytes = 32KB) for debugging
- Image store and image selector for the EPROM images in flash (replaced the earlier BSON-based EPROM library)
- has RTT debug output, NOT UART!

**Important Files:**
- `EP/src/epromEmulator.h` - Core EPROM emulation on Core 1
- `EP/src/EpromLoader.h` - Image loading and management on Core 0
- `EP/src/image_store_config.json` - which images go into the image store

### WP (Wireless Processor)
- **Processor:** RP2350 on Pico2W (Cortex-M33, more RAM/speed than RP2040)
- **SDK:** Same Pico SDK as EP
- **Source:** `WP/src/`

**Capabilities:**
- Receives ECU data stream from EP via UART
- GPS integration (uBlox NEO-8, 10Hz position/velocity)
- Logs to micro SD card using LittleFS filesystem
- Future: WiFi log upload, Bluetooth UI, OTA updates
- has RTT debug output, NOT UART!

**Important Files:**
- `WP/src/` - WP firmware source

### Tools & Python Scripts

**Log Decoder:**
```bash
# Decode binary logs to HDF5 format
uv run tools/logtools/decoder/decodelog.py <logfile> --format h5 -o output.h5

# Verify HDF5 output
uv run tools/logtools/decoder/verify_hdf5.py output.h5

# Run visualizer
uv run tools/logtools/viz/viz.py output.h5
```

**Key Tools:**
- **Build-time tools** (in `tools/src/`):
  - `build-image-store.py` - Builds image store artifacts (slot files, image_store.bin) from image_store_config.json
  - `h2py.py` - Converts C header log IDs to Python
  - `generate_encoder.py` - Generates log encoding code
- **Runtime log analysis tools** (in `tools/logtools/`):
  - `decoder/decodelog.py` - Decodes binary logs to HDF5 or text
  - `decoder/verify_hdf5.py` - Validates HDF5 log structure and content
  - `viz/viz.py` - Interactive visualization tool for HDF5 logs

### EPROM Library (eprom_lib)

JSON description files for stock Aprilia EPROMs (549USA, RP58, etc.).

**Note:** Repository does not include .bin files - users must source separately.

## Log System Architecture

All log event IDs (ECU, EP, WP and general) are defined in one header, `lib/inc/log_ids.h`.
The build turns it into the Python constants (`Logsyms.py`, plus `LogMeta.py`) that the decoder uses.

**Log Format:** Binary stream of event IDs followed by data bytes. Events include 16-bit timestamps (2μs resolution, wraparound handled). HDF5 output provides unified nanosecond time axis across all events.

## Directory Structure

```
umod4/
├── ecu/               - HC11 assembly firmware for ECU
├── EP/                - RP2040/2350 EPROM emulator
├── WP/                - RP2040/2350 wireless/logging processor
├── eprom_lib/         - JSON descriptions of stock EPROMs
├── tools/             - Python build tools and log decoders
├── lib/               - Shared C libraries (src/, inc/)
├── cmake/
│   └── toolchains/    - Cross-compiler toolchain definitions
│       ├── arm-none-eabi.cmake   - ARM Cortex-M toolchain
│       └── host.cmake            - Host system toolchain
└── build/             - Generated build artifacts (safe to delete)
```

## Hardware Integration

**Debug Probes:**
- Uses Raspberry Pi Debug Probe (CMSIS-DAP)
- VS Code launch.json configured for OpenOCD debugging
- On WSL2: Use usbipd to attach debug probe to WSL
- When debugging the EP on 4V1 PCB, SPARE2 must be grounded to inhibit WP from using SWD to talk to EP

**Flashing:**
```bash
# Via debugger (from VS Code)
F1 -> "Debug: Select And Start Debugging" -> "*** EP: Launch CMSIS-DAP"

# Or drag-and-drop .uf2 files to Pico in BOOTSEL mode
```

## Development Environment Requirements

**Prerequisites (from BUILDING.md):**
- Linux (Ubuntu WSL2, Linux Mint, or Raspberry Pi OS)
- VS Code with extensions: C/C++, CMake, Cortex-debug, MemoryView, Python
- m68hc11-elf toolchain (built from binutils source, installed to ~/.local/bin)
- arm-none-eabi-gcc 15.2.rel1 (installed to /opt/arm/arm-none-eabi/15.2.rel1)
- OpenOCD (built from raspberrypi fork for RP2xxx support)
- Raspberry Pi Pico SDK 2.3.0 (at ~/projects/pico-sdk/2.3.0)
- uv (installs and manages the Python environment, Python 3.12; see BUILDING.md)

**Directory Layout Assumption:**
The build system expects this specific structure:
```
~/projects/
├── pico-sdk/2.3.0/    - Pico SDK (version configurable in CMakeLists.txt)
├── pico-examples/     - Optional SDK examples
├── picotool/          - Picotool (built and installed to ~/.local/bin)
├── openocd/           - OpenOCD from raspberrypi GitHub
└── umod4/             - This project
```

## Common Development Tasks

**Changing Pico SDK Version:**
Edit top-level `CMakeLists.txt`, set `PICO_SDK_VERSION` variable, ensure SDK exists at `~/projects/pico-sdk/<version>`.

**Changing ARM Toolchain:**
Edit `cmake/toolchains/arm-none-eabi.cmake`, update `ARM_NONE_EABI_VERSION` variable.

**Modifying Log Event IDs:**
1. Edit `lib/inc/log_ids.h` file
2. Update `tools/logtools/decoder/decodelog.py` decoder logic for new events
3. Rebuild (F7 or `ninja`): the build regenerates the Python constants (`Logsyms.py`, `LogMeta.py` in `build/python_packages/`) from `log_ids.h` automatically, and every component includes the header from the same directory

## Key Technical Details

**Engine Operation**
The engine is a 60 degree V twin 4 stroke.
The crank sensor generates an interrupt every 60 degrees of rotation.
The CAM sensor indicates that the next crankshaft event will be crankshaft reference event 0, or CRID 0.
Subsequent crank events go from CRID1 through CRID11, when a CAM event will occur, and the entire CRID0..CRID11 cycle will repeat as long as the engine is running.

**EPROM Emulation Timing:**
- HC11 E-clock: 2MHz
- Bus cycle: 500ns
- EP Core 1 responds to read/write within HC11 timing requirements
- All 27 GPIO pins used for address/data/control lines

**UART Communication:**
- EP → WP: 2.5 megabaud baud, 32-bit transfers (log ID + up to 2 bytes of data)

**Filesystem:**
- LittleFS on micro SD card (wear-leveling, power-fail resilient)
- More reliable than FAT for embedded systems, but can get extremely slow when it gets a lot of data in the filesystem.

**GPS:**
- uBlox NEO-8 module
- 10Hz update rate for position and velocity
- UART interface to WP

## Known Issues & Limitations

**VS Code/CMake Bug:**
Cannot select debug targets normally due to ExternalProject_Add() preventing VS Code from finding executables. Workaround: Use F1 -> "Debug: Select And Start Debugging" and manually pick launch configuration.

**"Clean Rebuild" does not actually clean EP/WP/etc.:**
F1 -> "CMake: Clean Rebuild" only cleans files tracked by the top-level superbuild project (its own stamp/tracking files). Each `ExternalProject_Add()` subproject (EP, WP, ecu, tools, SwdReflash, WpUsbBoot, ap_proxy, picotool) is a fully independent nested CMake+Ninja project under `build/<name>/`, with its own `build.ninja` and `CMakeCache.txt` — the top-level `clean` target has no knowledge of what's inside those directories and cannot reach them. A "Clean Rebuild" can complete successfully while leaving every subproject's actual compiled artifacts (e.g. `build/EP/EP.uf2`) completely untouched, same file, same timestamp, as before.
To force a genuine full rebuild of one subproject: `rm -rf build/<name>` and rebuild. For a fast, real (non-fake) rebuild that just needs to bump a stale artifact timestamp: touch one of that subproject's own source files and rebuild — this triggers a real incremental recompile+relink without recompiling everything.

## Coding Conventions

**Editing:**
- do not leave trailing whitespace when editing
- tab spacing is 4

**Assembly (ECU):**
- 68HC11 assembly syntax
- Heavy use of comments for reverse-engineered sections
- See annotated `ecu/src/UM4.S` for example

**C/C++ (EP/WP):**
- Pico SDK conventions
- Dual-core aware: Use appropriate core for timing-critical vs. background tasks
- No panic() calls in EP (log errors instead to reach WP logfile)

**Python (Tools):**
- Run with `uv run <script>` (the project environment is `.venv`, managed by uv)
- Add or change dependencies in `pyproject.toml`, then run `uv lock`

## Testing

### Automated Test Harness

The project has an automated hardware-in-the-loop test harness in `tests/`. It requires physical hardware: a umod4 device connected via CMSIS-DAP debug probe, and an ap_proxy (Pico W USB serial HTTP proxy).

**Running tests:**

```bash
# Run all suites (provisioning first — erases WP flash)
uv run tests/runner.py --sta-ssid MyNetwork --sta-password MyPass --device-name umod4_test_hw

# Run specific suites (skips provisioning, board must already be flashed)
uv run tests/runner.py test_basic test_wifi

# Pre-flight checks only (USB devices, tools, build artifacts)
uv run tests/runner.py --preflight-only
```

Reports are written to `build/test_reports/run_YYYYMMDD_HHMMSS.md` and `build/test_reports/latest.md`.

**Test suites** (in `tests/suites/`):

* `test_basic` — boot info, SD info, filesystem health, GPS
* `test_ep_swd` — EP processor SWD connectivity
* `test_wifi` — WiFi connectivity
* `test_ota_ep` / `test_ota_wp` — OTA firmware update flows
* `test_provisioning` — full device provisioning via ap_proxy (AP mode → WiFi setup)

**Harness components** (in `tests/harness/`):

* `preflight.py` — pre-run checks (USB devices, tools, build artifacts)
* `rtt.py` — RTT output capture via OpenOCD
* `openocd.py` — OpenOCD process management
* `ap_proxy.py` — USB serial protocol to the ap_proxy Pico W
* `usbipd.py` / `usb_ids.py` — WSL2 USB attachment helpers

**Hardware requirements:**

* CMSIS-DAP debug probe (2e8a:000c) attached to WSL
* ap_proxy device (1209:0001) attached to WSL
* Build artifacts present: `build/WP/WP.uf2`, `build/EP/EP.uf2`, `build/WpUsbBoot/WpUsbBoot`

### Manual Verification (motorcycle testing)

For ECU/engine validation (no automated equivalent):
1. Flash EP and WP firmware via debugger
2. Install in bike ECU, verify engine runs correctly
3. Verify data logging to SD card
4. Extract and decode logs with `tools/logtools/decoder/decodelog.py`
5. Validate HDF5 output with `tools/logtools/decoder/verify_hdf5.py`
6. Visualize with `tools/logtools/viz/viz.py`
