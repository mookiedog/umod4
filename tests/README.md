# umod4 Test Harness

## Prerequisites

- Debug probe (Raspberry Pi Debug Probe) connected to WP
- OpenOCD installed at `/usr/local/bin/openocd`
- Python 3.10+

## Running the Tests

### Step 1: Build

Press **F7** in VS Code to build the WP firmware.

### Step 2: Flash

Run the VS Code task:

**Ctrl+Shift+P → "Tasks: Run Task" → "WP: Flash and Run (no debugger)"**

This flashes the firmware and reboots the WP. OpenOCD exits immediately, leaving the chip running and the SWD port free.

> **Note:** If you have a VS Code debug session active, disconnect it first — only one SWD connection is allowed at a time.

### Step 3: Run the Test Suite

From a terminal in the project root:

```bash
python3 tests/runner.py
```

To run a single suite by name:

```bash
python3 tests/runner.py test_basic
```

### Expected Output

```
umod4 test harness
------------------
Starting OpenOCD...
RTT ready.

[suites.test_basic]
  PASS  ping
  PASS  version  VFY: version PASS bt="..."
  PASS  status   VFY: status PASS uptime_ms=... heap_remaining=... lfs_mounted=1
  PASS  lfs_test

OK  4/4 passed
```

## How It Works

The harness communicates with the WP firmware over RTT (Real Time Transfer) via SWD:

- The runner launches its own OpenOCD subprocess and waits for it to find the RTT control block in WP RAM.
- Each RTT channel is exposed as a TCP port (`9000 + channel number`).
- Test suites send ASCII commands to the **WP_VFY** channel (port 9001) and read back `VFY: <cmd> PASS/FAIL [key=val ...]` responses.
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
