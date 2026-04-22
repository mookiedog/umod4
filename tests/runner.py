#!/usr/bin/env python3
"""
umod4 automated test runner.

Usage:
    build/.venv/bin/python3 tests/runner.py              # flash WP then run all suites
    build/.venv/bin/python3 tests/runner.py test_basic   # flash WP then run one suite
    build/.venv/bin/python3 tests/runner.py --no-flash   # skip flash, run all suites

The build venv is required because test_ota_ep imports DeviceClient which needs 'requests'.
All other suites use only stdlib.

The runner:
  1. Flashes WP with the latest firmware via tools/flash_WP (skippable with --no-flash)
  2. Resets WP via OpenOCD (guarantees clean boot regardless of prior state)
  3. Waits for RTT to come up
  4. Runs each test suite in order, passing a shared context dict between them
  5. Prints a summary and exits with code 0 (all pass) or 1 (any fail)
"""

import sys
import os
import importlib
import subprocess
import time

# Allow 'from harness.xxx import ...' and 'from suites.xxx import ...'
sys.path.insert(0, os.path.dirname(__file__))

from harness.openocd import OpenOCD, OpenOCDError

PROJECT_ROOT       = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WP_ENTER_BOOTSEL   = os.path.join(PROJECT_ROOT, "tools", "wp_enter_bootsel")
WP_USB_BOOT_BINARY = os.path.join(PROJECT_ROOT, "build", "WpUsbBoot", "WpUsbBoot")
FLASH_WP           = os.path.join(PROJECT_ROOT, "tools", "flash_WP")
WP_BUILD_DIR       = os.path.join(PROJECT_ROOT, "build", "WP")


# -------------------------------------------------------------------------
class Results:
    def __init__(self):
        self._entries = []
        self._current = None

    def start(self, name):
        self._current = name

    def passed(self, name, detail=""):
        self._entries.append(("PASS", name, detail))
        tag = f"  {detail}" if detail else ""
        print(f"  PASS  {name}{tag}")

    def failed(self, name, detail=""):
        self._entries.append(("FAIL", name, detail))
        tag = f"  {detail}" if detail else ""
        print(f"  FAIL  {name}{tag}")

    @property
    def all_passed(self):
        return all(e[0] == "PASS" for e in self._entries)

    @property
    def counts(self):
        passed = sum(1 for e in self._entries if e[0] == "PASS")
        return passed, len(self._entries)


# -------------------------------------------------------------------------
SUITES = [
    "suites.test_basic",
    "suites.test_ep_swd",
    "suites.test_wifi",
    "suites.test_ota_ep",
    "suites.test_ota_wp",
]

def load_suites(names=None):
    if names:
        return [importlib.import_module(f"suites.{n.removeprefix('suites.')}") for n in names]
    return [importlib.import_module(s) for s in SUITES]


def flash_wp():
    """Put WP into BOOTSEL mode via SWD, then flash the latest firmware."""
    for label, path in [
        ("wp_enter_bootsel", WP_ENTER_BOOTSEL),
        ("flash_WP",         FLASH_WP),
    ]:
        if not os.path.isfile(path):
            print(f"ERROR: {label} script not found at {path}")
            sys.exit(1)
    if not os.path.isfile(WP_USB_BOOT_BINARY):
        print(f"ERROR: WpUsbBoot binary not found at {WP_USB_BOOT_BINARY}")
        sys.exit(1)
    if not os.path.isdir(WP_BUILD_DIR):
        print(f"ERROR: WP build directory not found at {WP_BUILD_DIR}")
        sys.exit(1)

    print("Entering WP BOOTSEL mode via SWD...", flush=True)
    result = subprocess.run([WP_ENTER_BOOTSEL, WP_USB_BOOT_BINARY])
    if result.returncode != 0:
        print(f"ERROR: wp_enter_bootsel failed (exit code {result.returncode})")
        sys.exit(1)

    print(f"Flashing WP from {WP_BUILD_DIR} ...", flush=True)
    result = subprocess.run([FLASH_WP, WP_BUILD_DIR])
    if result.returncode != 0:
        print(f"ERROR: flash_WP failed (exit code {result.returncode})")
        sys.exit(1)
    print("Flash complete.\n", flush=True)


# -------------------------------------------------------------------------
def main():
    args = sys.argv[1:]
    no_flash = "--no-flash" in args
    requested = [a for a in args if not a.startswith("--")]
    suites = load_suites(requested if requested else None)

    print("umod4 test harness")
    print("------------------")

    if no_flash:
        print("--no-flash: skipping firmware flash\n", flush=True)
    else:
        flash_wp()

    print("Starting OpenOCD...", flush=True)

    with OpenOCD(verbose=False) as ocd:
        try:
            ocd.wait_ready()
        except OpenOCDError as e:
            print(f"ERROR: {e}")
            sys.exit(1)

        # Guard against a pending TBYB warm-boot: if the last flash used
        # 'picotool load -p 1', the bootrom boots in TBYB mode, check_tbyb()
        # commits (2 s busy-wait) then watchdog-resets WP.  OpenOCD fires
        # "RTT ready" during the first (warm) boot, but VfyTask never runs
        # until the subsequent cold boot.  Sleeping here lets that entire
        # cycle complete before the first test command is sent.
        time.sleep(5.0)
        print("RTT ready.\n")

        results = Results()
        context = {}   # shared state across suites (e.g. wp_ip discovered by test_wifi)
        for suite in suites:
            print(f"[{suite.__name__}]")
            suite.run(ocd, results, context)
            print()

    passed, total = results.counts
    print(f"{'OK' if results.all_passed else 'FAILED'}  {passed}/{total} passed")
    sys.exit(0 if results.all_passed else 1)


if __name__ == "__main__":
    main()
