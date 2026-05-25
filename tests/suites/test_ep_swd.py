"""
EP SWD test suite — verifies the WP↔EP SWD communication path and flash
programming mechanism.

swd             — cached boot-time connectivity state (set at WP startup).
swd_test_connect — live round-trip: write/read test pattern to EP SRAM.
swd_test_flash  — full flash cycle: load flasher stub → write 4K scratch block
                   → release EP from reset → re-verify connectivity.

swd_test_flash proves the SWD flash-programming mechanism works end-to-end.
It is the underpinning of EP OTA (test_ota_ep) and EP image store tests.
"""

import json
import os

from harness.rtt import RttChannel, RttError
from harness.elf_symbols import read_symbols, ElfError


WP_VFY_CHANNEL = 1

SWD_TIMEOUT   = 15.0
FLASH_TIMEOUT = 60.0   # load + erase + program + release + reconnect

EP_ELF = os.path.join(os.path.dirname(__file__), "../../build/EP/EP")


def _unused_flash_scratch():
    """
    Return the address of the last 64K block in EP's unused flash region.
    Reads __unused_flash_start__ and __unused_flash_size__ from EP.elf.
    Raises ElfError on any failure.
    """
    syms = read_symbols(EP_ELF, ["__unused_flash_start__", "__unused_flash_size__"])
    start = syms["__unused_flash_start__"]
    size  = syms["__unused_flash_size__"]
    return start + size - 64 * 1024


def _state(reply, key):
    """Parse reply JSON and return data[key]['state'], or None on error."""
    try:
        return json.loads(reply).get(key, {}).get("state")
    except (json.JSONDecodeError, AttributeError):
        return None


def run(ocd, results, context):
    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:

        # ----------------------------------------------------------------
        # swd — cached boot-time connectivity state
        # ----------------------------------------------------------------

        results.start("swd")
        try:
            reply = vfy.command("swd", timeout=SWD_TIMEOUT)
            state = _state(reply, "swd")
            if state == "ready":
                results.passed("swd", reply)
            else:
                results.abort("swd", f"state={state} — SWD not ready: {reply}")
        except RttError as e:
            results.abort("swd", str(e))

        # ----------------------------------------------------------------
        # swd_test_connect — live round-trip to EP SRAM
        # ----------------------------------------------------------------

        results.start("swd_test_connect")
        try:
            reply = vfy.command("swd_test_connect", timeout=SWD_TIMEOUT)
            state = _state(reply, "swd_test_connect")
            if state == "ready":
                results.passed("swd_test_connect", reply)
            else:
                results.abort("swd_test_connect", reply)
        except RttError as e:
            results.abort("swd_test_connect", str(e))

        # ----------------------------------------------------------------
        # swd_test_flash — full flash programming cycle
        # ----------------------------------------------------------------

        results.start("swd_test_flash")
        try:
            scratch = _unused_flash_scratch()
        except ElfError as e:
            results.abort("swd_test_flash", f"ELF symbol read failed: {e}")
            return

        try:
            cmd = f"swd_test_flash 0x{scratch:08x}"
            reply = vfy.command(cmd, timeout=FLASH_TIMEOUT)
            state = _state(reply, "swd_test_flash")
            if state == "ok":
                results.passed("swd_test_flash", reply)
            else:
                results.failed("swd_test_flash", reply)
        except RttError as e:
            results.failed("swd_test_flash", str(e))
