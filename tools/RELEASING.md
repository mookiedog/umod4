# Release Process Guide

This document describes how to create releases for the tools and the firmware.

## Overview

The umod4 project uses **independent versioning** for different components:
- **Tools releases** (the server and the visualizer, in one bundle): Tagged as `tools-vX.Y.Z`
- **Firmware releases**: Tagged as `firmware-vX.Y.Z`

Both live in the same repository but follow separate release schedules.

## Releasing the Tools (server and visualizer)

The tools are distributed as one **source bundle**: a zip file containing the server, the visualizer and the decoder code, the exact package versions they were tested with (`pyproject.toml` and `uv.lock`), a sample log, launcher scripts and instructions.
There is one bundle for both programs, so a user always has a matching set.
The recipient only needs [uv](https://docs.astral.sh/uv/).
On the first run of each program uv downloads Python 3.12 (if the machine does not have it) and that program's packages, which are mostly Qt and amount to roughly 300 MB.
There are no per-platform executables and no GitHub Actions: the bundle is built by running a script by hand.

### Building a bundle

```bash
cd ~/projects/umod4
uv run tools/make_tools_bundle
```

This writes `build/dist/umod4-tools-<date>-<git hash>.zip`.
It lives inside `build/` like every other generated file, so git ignores it and deleting `build/` removes it (rebuilding takes a few seconds).
The version is marked `-dirty` if any bundled file has uncommitted changes, so commit first when making a real release.

The zip keeps the repository's own layout, because the server's "launch visualizer" button finds the visualizer by relative path.
It contains:
- `tools/server`: the server's runtime `.py` files (no tests, no dead code, no developer docs)
- `tools/logtools/viz` and `tools/logtools/decoder`: the tracked `.py` and `.yaml` files
- `tools/logtools/decoder/Logsyms.py`, generated on the spot from `lib/inc/log_ids.h` (no firmware build is needed)
- `tools/logtools/test_data/log_3.um4`, the sample log
- `pyproject.toml` and `uv.lock`
- `run_server.cmd`, `run_viz.cmd` (Windows, double-click) and `run_server.sh`, `run_viz.sh` (Linux), taken from `tools/bundle/`
- `README.txt`: the instructions for the recipient, taken from the template `tools/bundle/README.txt`

The launchers run `uv run --only-group server ...` and `uv run --only-group viz ...`.
The `--only-group` part installs just that program's packages and leaves out the other group and the test packages.

### Release Steps

1. **Make and test changes**, then commit them:

   ```bash
   uv run tools/logtools/viz/viz.py tools/logtools/test_data/log_3.um4
   git add tools/ && git commit -m "Add feature X, fix bug Y"
   ```

2. **Build the bundle** (see above). A release should not be marked `-dirty`.

3. **Test the zip on a machine that has only uv**, ideally a Windows machine or a Windows Sandbox: unzip it, follow `README.txt`, start the server and check that its window opens with an empty device list, then start the visualizer on the sample log, answer yes to converting it, and check that graphs appear.
   The test does not mean much on a machine that has the umod4 source tree, because that hides missing files.
   Note that only one server can run at a time: it needs UDP port 54549 and says so if another server already has it.

4. **Hand it out.**
   Either send the zip directly, or publish it: on GitHub go to Releases, draft a new release (optionally with a `tools-vX.Y.Z` tag), and attach the zip by hand.


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
vim WP/src/main.cpp

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
- `lib/inc/log_ids.h`
- `tools/logtools/decoder/decodelog.py` decoder logic
- Compatibility notes in release description

#### 3. Create Release

```bash
# Tag firmware release
git tag firmware-v1.6.0
git push origin firmware-v1.6.0

# Create GitHub Release
# Go to: https://github.com/mookiedog/umod4/releases/new
# Upload: build/EP/EP.uf2, build/WP/WP.uf2, build/WP/WP_partition.uf2,
# build/WP_erase/WP_erase.uf2 and build/ecu/UM4.bin
# Note: May require visualizer update if log format changed
```

---

## Troubleshooting

**`tools/make_tools_bundle` fails:**
- Run it from inside the repository, with uv: `uv run tools/make_tools_bundle` (the script asks git for its file list, so git must be installed)
- "expected file is missing" means a file the bundle needs was moved or deleted; the message names it

**The recipient's first run of a program is slow, or does not start:**
- The first run of each program downloads Python 3.12 (if the machine has none) and that program's packages (about 300 MB for the visualizer), so it needs an internet connection
- uv itself must be installed first; `README.txt` in the bundle explains how

**GitHub upload fails:**
- Check the file size is under 2 GB (it should be about 2 MB)
- Try uploading through the web interface instead
