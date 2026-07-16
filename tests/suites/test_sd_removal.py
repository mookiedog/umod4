"""
SD card ejection/reinsertion regression test.

Proves the hotplug state machine's GOING_OFFLINE transition properly resets
LogStore's in-memory state on unmount, so a freshly remounted card doesn't
get stuck refusing to open new logs. LogStore is a single object that
outlives mount cycles; before this fix, nothing reset active_log_number on
unmount, so whatever log was active at removal time stayed "active" forever
and every subsequent createLog() was refused (the "log N still active" bug).

Uses the "sd_simulate_removal" VFY command instead of physically ejecting
the card: it forces the hotplug state machine into GOING_OFFLINE, the same
code path a real removal takes from OPERATIONAL, then the state machine
naturally redetects the still-present card and remounts it.

Prerequisites: test_basic must have passed (SD + filesystem healthy, so
there's a real active log to compare before/after).
"""

import json
import time

from harness.rtt import RttChannel, RttError


WP_VFY_CHANNEL = 1


def _cmd(vfy, name, timeout=5.0):
    """Send a command and return parsed JSON, or raise RttError / ValueError."""
    reply = vfy.command(name, timeout=timeout)
    try:
        return json.loads(reply)
    except json.JSONDecodeError:
        raise ValueError(f"non-JSON reply to '{name}': {reply}")


def run(ocd, results, context):
    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:

        # ----------------------------------------------------------------
        # sd_removal_precondition — confirm a real active log exists before
        # simulating a removal, so the before/after comparison means something.
        # Polls rather than checking once: if a prior suite (e.g. test_logstore's
        # chunk-crossing test) just closed its own log, Logger's own background
        # task needs a moment to notice and open a fresh one -- that's normal
        # settling time, not a failure.
        # ----------------------------------------------------------------

        results.start("sd_removal_precondition")
        log_before = -1
        try:
            deadline = time.monotonic() + 10.0
            while True:
                data = _cmd(vfy, "filesystem")
                fs = data.get("filesystem", {})
                log_before = fs.get("active_log", -1)
                if fs.get("state") == "mounted" and log_before >= 0:
                    results.passed("sd_removal_precondition", f"active_log={log_before}")
                    break
                if time.monotonic() >= deadline:
                    results.abort("sd_removal_precondition", f"no active log to test against: {fs}")
                time.sleep(0.5)
        except (RttError, ValueError) as e:
            results.abort("sd_removal_precondition", str(e))

        # ----------------------------------------------------------------
        # sd_simulate_removal — force GOING_OFFLINE
        # ----------------------------------------------------------------

        results.start("sd_simulate_removal")
        try:
            data = _cmd(vfy, "sd_simulate_removal")
            state = data.get("sd_simulate_removal", {}).get("state", "")
            if state == "ok":
                results.passed("sd_simulate_removal")
            else:
                results.abort("sd_simulate_removal", f"unexpected reply: {data}")
        except (RttError, ValueError) as e:
            results.abort("sd_simulate_removal", str(e))

        # ----------------------------------------------------------------
        # sd_remount — wait for the hotplug state machine to cycle back to
        # OPERATIONAL. The card never physically left, so it should.
        # ----------------------------------------------------------------

        results.start("sd_remount")
        try:
            deadline = time.monotonic() + 15.0
            while True:
                data = _cmd(vfy, "sd")
                sd_state = data.get("sd", {}).get("state", "")
                if sd_state == "operational":
                    results.passed("sd_remount")
                    break
                if time.monotonic() >= deadline:
                    results.abort("sd_remount", f"did not reach operational within 15s (state={sd_state})")
                time.sleep(0.5)
        except (RttError, ValueError) as e:
            results.abort("sd_remount", str(e))

        # ----------------------------------------------------------------
        # sd_new_log_after_remount — the actual regression check: LogStore
        # must accept a fresh active log after remount, not refuse forever
        # with the previous session's stale active_log_number.
        # ----------------------------------------------------------------

        results.start("sd_new_log_after_remount")
        try:
            deadline = time.monotonic() + 10.0
            fs = {}
            while True:
                data = _cmd(vfy, "filesystem")
                fs = data.get("filesystem", {})
                log_after = fs.get("active_log", -1)
                if fs.get("state") == "mounted" and log_after >= 0 and log_after != log_before:
                    results.passed("sd_new_log_after_remount",
                        f"active_log {log_before} -> {log_after}")
                    break
                if time.monotonic() >= deadline:
                    results.failed("sd_new_log_after_remount",
                        f"no new log opened after remount (still {fs}) -- "
                        f"this is exactly the bug this test exists to catch")
                    break
                time.sleep(0.5)
        except (RttError, ValueError) as e:
            results.failed("sd_new_log_after_remount", str(e))
