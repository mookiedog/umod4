# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Umod4 is a data logging system for Aprilia Gen 1 motorcycle fuel injection ECUs. It replaces the ECU's EPROM with a sophisticated circuit board containing two ARM processors (EP and WP) that emulate the EPROM, log ECU data streams, correlate with GPS data, and provide future wireless capabilities.

## Build System Architecture

### CMake Superbuild Structure

This project uses CMake's **ExternalProject_Add** superbuild pattern because the Raspberry Pi Pico SDK can only be configured once per CMake namespace. Since the project requires two different board configurations (EP and WP), each must have its own namespace.

**Critical Build Constraints:**
- Never try to build this as a single unified CMake project
- Each external project (tools, ecu, eprom_lib, EP, WP) has its own build namespace
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
The build system creates a virtual environment at `build/.venv` and installs required packages (pymongo, JSON_minify, mmh3, numpy, h5py). Use `build/.venv/bin/python3` for running tools.

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
- `ecu/src/ECU_log.h` - ECU log event ID definitions

### EP (EPROM Processor)
- **Processor:** RP2040 (Cortex-M0+)
- **SDK:** Raspberry Pi Pico SDK 2.2.0 (configurable in top-level CMakeLists.txt)
- **Toolchain:** arm-none-eabi-gcc 14.2.rel1 (at `/opt/arm/arm-none-eabi/14.2.rel1`)
- **Source:** `EP/src/`

**Dual-Core Architecture:**
- **Core 1:** EPROM emulation - Responds to HC11 read/write requests at 2MHz bus speed using 27 GPIO pins
- **Core 0:** Flash management, UART communication with WP, image loading from SPI flash

**Key Capabilities:**
- Emulates 32KB EPROM with regions that can act as RAM (expands ECU's 512 bytes)
- Loads and combines multiple EPROM images from 16MB SPI flash partition
- Forwards ECU data stream to WP via UART at 921600 baud
- Logs all HC11 accesses to circular buffer (16,384 entries) for debugging
- BSON document-based EPROM library with metadata

**Important Files:**
- `EP/src/epromEmulator.h` - Core EPROM emulation on Core 1
- `EP/src/EpromLoader.h` - Image loading and management on Core 0
- `EP/src/bsonlib.h` - BSON document handling for EPROM library
- `EP/src/EP_log.h` - EP log event IDs

### WP (Wireless Processor)
- **Processor:** RP2350 on Pico2W (Cortex-M33, more RAM/speed than RP2040)
- **SDK:** Same Pico SDK as EP
- **Source:** `WP/src/`

**Capabilities:**
- Receives ECU data stream from EP via UART
- GPS integration (uBlox NEO-8, 10Hz position/velocity)
- Logs to micro SD card using LittleFS filesystem
- Future: WiFi log upload, Bluetooth UI, OTA updates

**Important Files:**
- `WP/src/` - WP firmware source

### Tools & Python Scripts

**Log Decoder:**
```bash
# Decode binary logs to HDF5 format
build/.venv/bin/python3 tools/logtools/decoder/decodelog.py <logfile> --format hdf5 -o output.h5

# Verify HDF5 output
build/.venv/bin/python3 tools/logtools/decoder/verify_hdf5.py output.h5

# Run visualizer
build/.venv/bin/python3 tools/logtools/viz/viz.py output.h5
```

**Key Tools:**
- **Build-time tools** (in `tools/src/`):
  - `bin-to-bson.py` - Converts EPROM .bin + .dsc (JSON) to BSON
  - `h2py.py` - Converts C header log IDs to Python
  - `generate_encoder.py` - Generates log encoding code
- **Runtime log analysis tools** (in `tools/logtools/`):
  - `decoder/decodelog.py` - Decodes binary logs to HDF5 or text
  - `decoder/verify_hdf5.py` - Validates HDF5 log structure and content
  - `viz/viz.py` - Interactive visualization tool for HDF5 logs

### EPROM Library (eprom_lib)

JSON description files for stock Aprilia EPROMs (549USA, RP58, etc.). Build system converts `.dsc` (JSON) + `.bin` files to BSON documents embedded in EP firmware.

**Note:** Repository does not include .bin files - users must source separately.

## Log System Architecture

The system uses a hierarchical log ID system with separate header files:
- `ecu/src/ECU_log.h` - ECU-generated events
- `EP/src/EP_log.h` - EP-generated events
- `WP/src/WP_log.h` - WP-generated events (GPS, filesystem)

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
- arm-none-eabi-gcc 14.2.rel1 (installed to /opt/arm/arm-none-eabi/14.2.rel1)
- OpenOCD (built from raspberrypi fork for RP2xxx support)
- Raspberry Pi Pico SDK 2.2.0 (at ~/projects/pico-sdk/2.2.0)
- Python 3.10+ with venv support

**Directory Layout Assumption:**
The build system expects this specific structure:
```
~/projects/
├── pico-sdk/2.2.0/    - Pico SDK (version configurable in CMakeLists.txt)
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
1. Edit appropriate `*_log.h` file (ECU_log.h, EP_log.h, or WP_log.h)
2. Run `build/.venv/bin/python3 tools/src/h2py.py` to regenerate Python constants
3. Update `tools/logtools/decoder/decodelog.py` decoder logic for new events
4. Rebuild affected components (all components include log headers from same directory)

**Adding New EPROM:**
1. Create `.dsc` JSON file in `eprom_lib/src/` describing EPROM metadata
2. Optionally add corresponding `.bin` file (not in repo, sourced separately)
3. CMake automatically converts to BSON at build time
4. BSON embedded in EP firmware flash partition

## Key Technical Details

**EPROM Emulation Timing:**
- HC11 E-clock: 2MHz
- Bus cycle: 500ns
- EP Core 1 responds to read/write within HC11 timing requirements
- All 27 GPIO pins used for address/data/control lines

**UART Communication:**
- EP → WP: 921600 baud, character pairs or packets up to 32 bytes
- WP RX timeout interrupt: ~35μs (32-bit period at 921600 baud)

**Filesystem:**
- LittleFS on micro SD card (wear-leveling, power-fail resilient)
- Faster and more reliable than FAT for embedded flash

**GPS:**
- uBlox NEO-8 module
- 10Hz update rate for position and velocity
- UART interface to WP

## Known Issues & Limitations

**From ToDo.md:**
- Missing FC_OFF log events during engine operation (investigate why not all ignition events logged)
- Missing log data at start of some files (likely timing issue with LittleFS writes)
- Need to track time when engine not rotating (80ms scheduler should emit timestamp if no crank events)
- Hardware.h only defines PCB 4V0 (needs update for 4V1)

**VS Code/CMake Bug:**
Cannot select debug targets normally due to ExternalProject_Add() preventing VS Code from finding executables. Workaround: Use F1 -> "Debug: Select And Start Debugging" and manually pick launch configuration.

## Coding Conventions

**Assembly (ECU):**
- 68HC11 assembly syntax
- Heavy use of comments for reverse-engineered sections
- See annotated `ecu/src/UM4.S` for example

**C/C++ (EP/WP):**
- Pico SDK conventions
- Dual-core aware: Use appropriate core for timing-critical vs. background tasks
- No panic() calls in EP (log errors instead to reach WP logfile)

**Python (Tools):**
- Use project venv: `build/.venv/bin/python3`
- Required packages auto-installed by CMake

## Testing

Currently no automated test suite. Testing happens on physical hardware (Aprilia Tuono motorcycle ECU with umod4 board installed).

**Verification Steps:**
1. Build succeeds without errors
2. Flash EP and WP firmware via debugger
3. Install in bike ECU, verify engine runs correctly
4. Verify data logging to SD card
5. Extract and decode logs with `tools/logtools/decoder/decodelog.py`
6. Validate HDF5 output with `tools/logtools/decoder/verify_hdf5.py`
7. Visualize with `tools/logtools/viz/viz.py`
