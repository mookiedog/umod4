# Release Process Guide

This document describes how to create releases for the visualizer and firmware.

## Overview

The umod4 project uses **independent versioning** for different components:
- **Visualizer releases**: Tagged as `viz-vX.Y.Z`
- **Firmware releases**: Tagged as `firmware-vX.Y.Z`

Both live in the same repository but follow separate release schedules.

## Releasing the Visualizer

The visualizer is distributed as **multi-platform executables** (Windows, Linux, macOS) built automatically by GitHub Actions.

### Prerequisites

1. Python virtual environment set up for local testing (happens automatically during CMake configure)
2. Test data in `tools/logtools/test_data/` (at least one `.um4` or `.log` file)

### Release Steps

#### 1. Make and Test Changes

```bash
# Make your changes
vim tools/logtools/viz/viz.py

# Test locally
build/.venv/bin/python3 tools/logtools/viz/viz.py tools/logtools/test_data/log_3.um4

# Commit when satisfied
git add tools/logtools/viz/
git commit -m "Add feature X, fix bug Y"
git push origin main
```

#### 2. Create and Push Git Tag

```bash
# Create annotated tag with version
git tag -a viz-v2.1.0 -m "Data Visualizer v2.1.0"

# Push tag to trigger automated build
git push origin viz-v2.1.0
```

**What happens next:**
1. GitHub Actions detects the `viz-v*` tag
2. Spawns 3 parallel build jobs (Windows, Linux, macOS)
3. Each job:
   - Builds PyInstaller executable for that platform
   - Packages with sample `.um4` log files
   - Creates platform-specific ZIP archive
4. Creates GitHub Release automatically with all 3 ZIP files attached

You can monitor the build progress at: https://github.com/mookiedog/umod4/actions

#### 3. Edit Release Notes (Optional)

After the automated release is created:

1. Go to: https://github.com/mookiedog/umod4/releases
2. Find your new release (e.g., `viz-v2.1.0`)
3. Click **Edit release**
4. Update the auto-generated notes (see template below)
5. Click **Update release**

**Note:** GitHub auto-generates release notes from commit messages. You can enhance them with the template below.

### Release Notes Template

Use this template when editing the auto-generated release notes:

```markdown
# Data Visualizer v2.1.0

## Downloads

Choose the version for your operating system:
- **DataVisualizer-Windows-v2.1.0.zip** - Windows 10 or later
- **DataVisualizer-Linux-v2.1.0.zip** - Ubuntu 20.04+ or similar
- **DataVisualizer-macOS-v2.1.0.zip** - macOS 10.13 (High Sierra) or later

## What's New
- Added injector bar tooltips showing duration and timing
- Fixed zoom bug when switching between streams
- Improved rendering performance for long logs

## Installation
1. Download the ZIP file for your platform
2. Extract the ZIP file
3. Run the DataVisualizer executable (no installation needed)

## Sample Files
Each release includes sample `.um4` log files for testing. The visualizer will automatically offer to convert them to HDF5 format when opened.

## Compatibility
Compatible with firmware v1.4.0 and later.

## Running from Source (Alternative)
If you prefer to run from source code:
```bash
git clone https://github.com/mookiedog/umod4.git
cd umod4
pip install -r tools/logtools/viz/requirements.txt
python tools/logtools/viz/viz.py <logfile.h5>
```

## Known Issues
- None

---

**Full changelog**: https://github.com/mookiedog/umod4/compare/viz-v2.0.0...viz-v2.1.0
```

### Version Numbering

Use semantic versioning (MAJOR.MINOR.PATCH):
- **MAJOR** (v3.0.0): Breaking changes, incompatible with old log formats
- **MINOR** (v2.1.0): New features, backward compatible
- **PATCH** (v2.0.1): Bug fixes only

### Release Frequency

- **Patch releases**: As needed for critical bugs (hours to days)
- **Minor releases**: When features accumulate (weeks)
- **Major releases**: Major redesigns or log format changes (months)

---

## Releasing Firmware

Firmware releases are less frequent and follow a different process since they require the full build toolchain.

### Prerequisites

1. ARM toolchain installed
2. m68hc11 toolchain installed
3. Pico SDK configured
4. Hardware available for testing

### Release Steps

#### 1. Make and Test Changes

```bash
# Make firmware changes
vim ecu/src/UM4.S
vim EP/src/epromEmulator.h
vim WP/src/main.c

# Build
cd build
ninja

# Flash to hardware and test thoroughly
# (See main BUILDING.md for flashing instructions)

# Commit when testing passes
git add ecu/ EP/ WP/
git commit -m "Add oil pressure sensor support"
git push origin main
```

#### 2. Update Compatibility Notes

If log format changed, update:
- `ecu/src/ECU_log.h`, `EP/src/EP_log.h`, or `WP/src/WP_log.h`
- `tools/logtools/decoder/decodelog.py` decoder logic
- Compatibility notes in release description

#### 3. Create Release

```bash
# Tag firmware release
git tag firmware-v1.6.0
git push origin firmware-v1.6.0

# Create GitHub Release
# Go to: https://github.com/mookiedog/umod4/releases/new
# Upload: build/EP/EP.uf2, build/WP/WP.uf2, build/ecu/ECU.bin
# Note: May require visualizer update if log format changed
```

---

## Manual Local Builds (Optional)

For testing or one-off builds, you can use the local build script:

```bash
# Build for your current platform only
./tools/build-release.sh v2.1.0-test
```

This creates a local build in `build/releases/visualizer/v2.1.0-test/` but does NOT create a GitHub release. Useful for:
- Testing changes before pushing a tag
- Creating custom builds for specific platforms
- Debugging build issues

**Note**: The script builds for the platform you're running on (Linux on WSL2, Windows on Windows, etc.). For official releases with all platforms, use the GitHub Actions workflow by pushing a tag.

---

## Troubleshooting

**Build script fails:**
- Ensure you're in project root
- Check that `build/.venv` exists (run CMake configure if not)
- Verify test data exists in `tools/logtools/test_data/`

**PyInstaller import errors:**
- Install PyInstaller: `build/.venv/bin/pip install pyinstaller`
- Check all dependencies installed: `pip install -r tools/logtools/viz/requirements.txt`

**Executable won't run on Windows:**
- Test with Wine first on Linux
- Check Windows Defender isn't blocking it
- Ensure antivirus allows unsigned executables

**GitHub upload fails:**
- Check file size <2GB (shouldn't be an issue)
- Try uploading via web interface instead of git-release
- Split into multiple files if needed
