"""
EP SWD test suite — verifies the WP↔EP SWD communication path.

Phase 0: Prerequisites (SPARE2 check).
Phase 1: EP halted in bootrom — blank-board safe, pure SWD driver.
Phase 2: Flash write — prove we can program EP flash before releasing EP.
Phase 3: Release reset — verify EP boots and SWD survives the transition.
"""

import os
from harness.rtt import RttChannel, RttError
from harness.elf_symbols import read_symbols, ElfError


WP_VFY_CHANNEL = 1

# Generous timeout for SWD operations (connect + memory read/write)
SWD_TIMEOUT = 15.0
# Flash write can take up to ~5 s (erase + program + verify)
FLASH_TIMEOUT = 30.0

# Path to EP ELF, relative to the project root (where runner.py lives).
EP_ELF = os.path.join(os.path.dirname(__file__), "../../build/EP/EP")


def _unused_flash_scratch():
    """
    Read __unused_flash_start__ and __unused_flash_size__ from EP.elf and
    return the address of the last 64K block in the unused region.
    Raises ElfError on any failure.
    """
    syms = read_symbols(EP_ELF, ["__unused_flash_start__", "__unused_flash_size__"])
    start = syms["__unused_flash_start__"]
    size  = syms["__unused_flash_size__"]
    # Use the last 64K slot so forward-allocation from the region start is safe.
    return start + size - 64 * 1024


def run(ocd, results, context):
    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:

        # ----------------------------------------------------------------
        # Phase 0 — Prerequisite: SPARE2 must not be grounded
        # ----------------------------------------------------------------

        results.start("swd_spare2_check")
        try:
            reply = vfy.command("swd_spare2_check", timeout=SWD_TIMEOUT)
            if "PASS" in reply:
                results.passed("swd_spare2_check", reply)
            else:
                results.failed("swd_spare2_check", reply)
                print("  NOTE: SPARE2 must be ungrounded (floating) for WP→EP SWD.")
                return
        except RttError as e:
            results.failed("swd_spare2_check", str(e))
            return

        # ----------------------------------------------------------------
        # Phase 1 — EP halted in bootrom
        # ----------------------------------------------------------------

        results.start("swd_connect_in_reset")
        try:
            reply = vfy.command("swd_connect_in_reset", timeout=SWD_TIMEOUT)
            if "PASS" in reply:
                results.passed("swd_connect_in_reset", reply)
            else:
                results.failed("swd_connect_in_reset", reply)
                return   # no point continuing without a working SWD connection
        except RttError as e:
            results.failed("swd_connect_in_reset", str(e))
            return

        results.start("swd_read_flash_in_reset")
        try:
            reply = vfy.command("swd_read_flash_in_reset", timeout=SWD_TIMEOUT)
            if "PASS" in reply:
                results.passed("swd_read_flash_in_reset", reply)
            else:
                results.failed("swd_read_flash_in_reset", reply)
        except RttError as e:
            results.failed("swd_read_flash_in_reset", str(e))

        results.start("swd_ram_roundtrip")
        try:
            reply = vfy.command("swd_ram_roundtrip", timeout=SWD_TIMEOUT)
            if "PASS" in reply:
                results.passed("swd_ram_roundtrip")
            else:
                results.failed("swd_ram_roundtrip", reply)
        except RttError as e:
            results.failed("swd_ram_roundtrip", str(e))

        results.start("swd_load_swdreflash")
        try:
            reply = vfy.command("swd_load_swdreflash", timeout=SWD_TIMEOUT)
            if "PASS" in reply:
                results.passed("swd_load_swdreflash", reply)
            else:
                results.failed("swd_load_swdreflash", reply)
                return   # can't do flash write without swdreflash loaded
        except RttError as e:
            results.failed("swd_load_swdreflash", str(e))
            return

        # ----------------------------------------------------------------
        # Phase 2 — Flash write
        # ----------------------------------------------------------------

        results.start("swd_write_flash")
        try:
            scratch = _unused_flash_scratch()
        except ElfError as e:
            results.failed("swd_write_flash", f"ELF symbol read failed: {e}")
            return

        try:
            cmd = f"swd_write_flash 0x{scratch:08x}"
            reply = vfy.command(cmd, timeout=FLASH_TIMEOUT)
            if "PASS" in reply:
                results.passed("swd_write_flash", reply)
            else:
                results.failed("swd_write_flash", reply)
        except RttError as e:
            results.failed("swd_write_flash", str(e))

        # ----------------------------------------------------------------
        # Phase 3 — Release reset, verify SWD survives the transition
        # ----------------------------------------------------------------

        results.start("swd_release_reset")
        try:
            reply = vfy.command("swd_release_reset", timeout=SWD_TIMEOUT + 2.0)
            if "PASS" in reply:
                results.passed("swd_release_reset", reply)
            else:
                results.failed("swd_release_reset", reply)
                return
        except RttError as e:
            results.failed("swd_release_reset", str(e))
            return

        results.start("swd_reconnect_after_boot")
        try:
            reply = vfy.command("swd_reconnect_after_boot", timeout=SWD_TIMEOUT)
            if "PASS" in reply:
                results.passed("swd_reconnect_after_boot", reply)
            else:
                # SWD unresponsive after boot = bad image — hard abort
                results.failed("swd_reconnect_after_boot", reply)
                print("  NOTE: EP SWD unresponsive after boot — bad image or EP hang.")
                return
        except RttError as e:
            results.failed("swd_reconnect_after_boot", str(e))
            return
