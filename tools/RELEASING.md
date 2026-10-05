# Release Process Guide

This document describes how to create releases for the visualizer and firmware.

## Overview

The umod4 project uses **independent versioning** for different components:
- **Visualizer releases**: Tagged as `viz-vX.Y.Z`
- **Firmware releases**: Tagged as `firmware-vX.Y.Z`

Both live in the same repository but follow separate release schedules.

## Releasing the Visualizer

The visualizer is distributed as a **source bundle**: a zip file containing the visualizer and decoder code, the exact package versions it was tested with (`pyproject.toml` and `uv.lock`), a sample log, and instructions.
The recipient only needs [uv](https://docs.astral.sh/uv/).
On the first run uv downloads Python 3.12 (if the machine does not have it) and the packages, which are mostly Qt and amount to roughly 300 MB.
There are no per-platform executables and no GitHub Actions: the bundle is built by running a script by hand.

### Building a bundle

```bash
cd ~/projects/umod4
uv run tools/make_viz_bundle
```

This writes `build/dist/umod4-viz-<date>-<git hash>.zip`.
It lives inside `build/` like every other generated file, so git ignores it and deleting `build/` removes it (rebuilding takes a few seconds).
The version is marked `-dirty` if any bundled file has uncommitted changes, so commit first when making a real release.

The zip contains:
- `logtools/viz` and `logtools/decoder`: the tracked `.py` and `.yaml` files
- `logtools/decoder/Logsyms.py`, generated on the spot from `lib/inc/log_ids.h` (no firmware build is needed)
- `pyproject.toml` and `uv.lock`
- `log_3.um4`, the sample log
- `README.txt`: the instructions for the recipient, taken from the template `tools/TRY_ME.txt`

The recipient's command is `uv run --project . --only-group viz logtools/viz/viz.py log_3.um4`.
The `--only-group viz` part installs just the visualizer's packages and leaves out the server and test packages.

### Release Steps

1. **Make and test changes**, then commit them:

   ```bash
   uv run tools/logtools/viz/viz.py tools/logtools/test_data/log_3.um4
   git add tools/logtools/ && git commit -m "Add feature X, fix bug Y"
   ```

2. **Build the bundle** (see above). A release should not be marked `-dirty`.

3. **Test the zip on a machine that has only uv**, ideally a Windows machine: unzip it, run the command in `README.txt`, answer yes to converting the sample log, and check that graphs appear.
   The test does not mean much on a machine that has the umod4 source tree, because that hides missing files.

4. **Hand it out.**
   Either send the zip directly, or publish it: on GitHub go to Releases, draft a new release (optionally with a `viz-vX.Y.Z` tag), and attach the zip by hand.

   Note: until the old GitHub Actions workflow is deleted (Phase 5 of `doc/uv-migration-plan.md`), do not push a `viz-v*` tag: it would start that obsolete workflow.


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
