# Automated Testing

## Goal

I would like to explore a method that allows for automated testing of parts of this system on real hardware.

The basic premise is that there would be an SWD debugger connected to the PC.
The debugger would typically be connected to the WP since that would allow the debugger to watch both WP and EP RTT output.
I would like to generate a series of tests that could be run from a bash shell that would detect the SWD debugger, connect to the WP, and start the WP running (which will restart the EP as a side effect).

From there I would like the script to watch the RTT output from the appropriate channel EP_VFY, EP_STDIO, WP_VFY, WP_STDIO to make sure that certain things happened.

It also seems valuable that this test harness would be able to invoke an API exposed by the server program.
The goal here would be to verify things like:

* rebuild the system
* ability to reflash the WP via the server
* ability to reflash the EP via the server
* watch the EP boot via RTT to verify that it started Core1 and that core1 started reporting data

In the future, it might be useful to exercise the web interface to make sure that everything that is supposed to work is still working

---

## Board State and Test Flow

The test harness always follows the same four-phase sequence regardless of what state
the board is in. The snapshot taken in phase 1 drives what happens in phase 4.

### Phase 1: Snapshot

Attempt to read the board's current state:

* Try SWD connect to WP. If WP is unresponsive or in ROM bootloader mode, the board
  is blank — note this and proceed.
* If WP is running, send `ping` on WP_VFY to confirm firmware is alive.
* Read EP image selector (slot 0 at `0x10200000`). If all 0xFF, EP flash is blank.
* Save any valid EP image selector and image store contents to the PC.

A blank board produces an empty snapshot. A populated board produces a full snapshot.
Either way, phase 2 proceeds identically.

### Phase 2: Provision to Known-Good Test State

Bring the board to a fully provisioned, known-good baseline regardless of its prior state:

1. Flash WP firmware via SWD (OpenOCD → WP directly)
2. Verify WP boots and VFY channel responds to `ping`
3. Flash EP firmware via WP's SWD control
4. Write a standard set of test images to EP flash
5. Write a generic test image selector to slot 0
6. Verify EP boots correctly and Core1 starts

On a blank board this is full provisioning from scratch. On a populated board it
overwrites everything — which is intentional, since the snapshot already preserved it.

### Phase 3: Run All Tests

With the board in a known state, run the full test suite. All tests — including
full reflash — are part of this phase. There is no distinction between "safe" and
"destructive" tests at this point because the snapshot guarantees recoverability.

The full reflash test (WP OTA + EP reflash + image store rewrite) is a **hard release
gate** and always runs as part of the suite.

### Phase 4: Restore

* If the snapshot was valid (populated board): restore the original image selector
  and image store unconditionally, whether the tests passed or failed.
* If the snapshot was empty (blank board): leave the board in its provisioned test
  state, or optionally wipe it back to blank.

This four-phase structure means the harness is equally correct for a brand-new board
off the production line and for a developer's board with a real image library on it.

---

## How the Test Harness Connects

The test harness is a bash script (or Python wrapper) running on the PC. It communicates
with the WP via two mechanisms:

### RTT Channels (via OpenOCD)

OpenOCD exposes each RTT channel as a TCP server. The harness connects using `nc` (netcat)
or a Python socket:

```bash
# Start OpenOCD with one TCP port per RTT channel
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
    -c "rtt setup 0x20000000 0x80000 \"SEGGER RTT\"" \
    -c "rtt start" \
    -c "rtt server start 9090 0" \   # WP_STDIO  (debug output, read-only)
    -c "rtt server start 9091 1" \   # WP_VFY    (test commands in, VFY results out)
    -c "rtt server start 9092 2"     # WP_SHELL  (human interactive shell)

# Send a test command and read the response
echo "ping" | nc -q1 localhost 9091
# → VFY: PING PASS
```

For test scripts, a small Python helper is better than raw `nc` because `nc`'s timeout
behavior varies across Linux versions. But `nc` is useful for quick manual verification.

### HTTP API (via WiFi)

Once WiFi is running, the harness can call WP's HTTP server directly using `curl`.
This is used for bulk operations (uploading firmware, images) that are awkward over RTT.

---

## VFY Channel Protocol

The WP_VFY channel (RTT channel 1) is the primary automation interface. It is fully
bidirectional and ASCII on both sides, so it can be driven manually with `nc` for
debugging the test infrastructure itself.

**Command format (host → device):**

```text
<command> [arg1] [arg2] ...
```

**Response format (device → host):**

```text
VFY: <TEST_ID> PASS [key=value ...]
VFY: <TEST_ID> FAIL [key=value ...]
VFY: <TEST_ID> INFO [key=value ...]
```

**Important:** `VFY()` uses `BLOCK_IF_FIFO_FULL`. Only call it in response to an explicit
test command — never unconditionally during boot or normal operation. If no debugger is
draining the buffer, the calling task blocks permanently. Boot status belongs on WP_STDIO.

### Basic Command Set

These are implemented first to verify the channel infrastructure before building
test-specific commands on top.

| Command | Response | Purpose |
| --- | --- | --- |
| `ping` | `VFY: PING PASS` | Proves channel is alive |
| `version` | `VFY: VERSION PASS fw=1.4.2 ep=1.2.0` | Proves firmware identity |
| `status` | `VFY: STATUS PASS sd=mounted wifi=connected ep=running` | Catches most boot regressions |
| `lfs_test` | `VFY: LFS_TEST PASS` (with intermediate INFO lines) | LittleFS smoke test, self-contained |
| `epread <addr> <len>` | `VFY: EPREAD PASS addr=0x10200000 len=32 sha=a3f92c44` | Foundation for EP flash tests |

---

## Test Surfaces

### EP Flash Management (via WP SWD control)

This entire surface tests WP's ability to read, erase, and write EP's SPI flash — both image
slots and the image selector itself. All operations go through WP's existing SWD/flash API.

**Prerequisites / Setup:**

* Snapshot the current image selector (slot 0, 32KB BSON doc at flash offset 0x10200000)
  before any destructive test — restore it unconditionally at teardown, pass or fail.
* Identify 2-3 unused slots by parsing the image selector's `"images"` array and finding
  slot numbers not listed there. These are safe to use as scratch space.

**Slot-level erase/write verify:**

1. Erase one unused slot — read it back and confirm all 0xFF
2. Write a known test pattern (e.g., incrementing bytes) to that slot
3. Read it back and verify byte-for-byte match
4. Erase again and verify 0xFF — confirms erase/write/read round-trip works

**Test image upload:**

1. Upload a normal test image into a scratch slot
2. Upload a protected-flag test image into a second scratch slot
3. Upload an intentionally malformed/truncated image into a third scratch slot (to test
   EP's error handling during image selection)

**Image selector manipulation:**

1. Generate a test BSON image selector document that references only the test images
   above, with specific selection criteria
2. Flash the test selector to slot 0
3. Trigger EP reboot (via WP GPIO control or SWD reset)
4. Capture EP RTT (EP_VFY + EP_STDIO) and verify:
   * EP reports the expected image was selected
   * Core1 starts and reports data
   * Protected image protection flags were honored
   * Malformed image was rejected gracefully (not silently used)
5. Repeat with different selector configurations to exercise selection priority logic

**Teardown:**

* Restore original image selector to slot 0
* Verify EP boots correctly with the restored selector
* Erase scratch slots used during testing

**What this catches:**

* Regressions in WP's SWD flash write path
* BSON parser bugs in EP's image selection logic
* Protection flag enforcement
* EP error handling for corrupt images
* Any breakage in the EP reboot → image-load → Core1-start sequence

---

### EP Boot Sequence

* Watch EP_VFY + EP_STDIO after reset
* Verify Core0 initializes flash, finds image selector, selects and loads image
* Verify Core1 starts and reports first data within expected time window
* Verify VTA messages appear at expected cadence (frequency + timestamp delta)

---

### WP LittleFS

* Create a test file, write known data, close it
* Reopen, read back, verify contents match
* Delete the file, verify it no longer appears in directory listing
* Verify SD free space after deletion returns to pre-test value

---

### WP Self-Update ("Cannot Brick" Gate)

This is a hard release gate. A release image must be able to replace itself.

1. Upload current WP.uf2 to the WP via the HTTP upload endpoint
2. Observe reboot via RTT disconnect + reconnect
3. Verify new image boots and emits expected WP_VFY boot messages
4. Verify the upload endpoint still accepts a POST (does not need to complete, just respond)

If this test fails, the build is not releasable.

---

### WiFi / HTTP Endpoint Smoke Tests

* Verify each API endpoint returns the expected response shape (not just HTTP 200)
* Verify upload endpoint rejects malformed requests gracefully
* Verify AP mode comes up and captive portal redirects work
* Verify WiFi reconnect after deliberate disconnect
