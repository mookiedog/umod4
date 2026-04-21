#!/usr/bin/env python3
"""
umod4 automated test runner.

Usage:
    python3 tests/runner.py              # run all suites
    python3 tests/runner.py test_basic   # run one suite by name

The runner:
  1. Starts OpenOCD and waits for RTT to come up
  2. Runs each test suite
  3. Prints a summary and exits with code 0 (all pass) or 1 (any fail)
"""

import sys
import os
import importlib
import time

# Allow 'from harness.xxx import ...' and 'from suites.xxx import ...'
sys.path.insert(0, os.path.dirname(__file__))

from harness.openocd import OpenOCD, OpenOCDError


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
]

def load_suites(names=None):
    if names:
        return [importlib.import_module(f"suites.{n.removeprefix('suites.')}") for n in names]
    return [importlib.import_module(s) for s in SUITES]


# -------------------------------------------------------------------------
def main():
    requested = sys.argv[1:]
    suites = load_suites(requested if requested else None)

    print("umod4 test harness")
    print("------------------")
    print("Starting OpenOCD...", flush=True)

    with OpenOCD(verbose=False) as ocd:
        try:
            ocd.wait_ready()
        except OpenOCDError as e:
            print(f"ERROR: {e}")
            sys.exit(1)

        print("RTT ready.\n")

        results = Results()
        for suite in suites:
            print(f"[{suite.__name__}]")
            suite.run(ocd, results)
            print()

    passed, total = results.counts
    print(f"{'OK' if results.all_passed else 'FAILED'}  {passed}/{total} passed")
    sys.exit(0 if results.all_passed else 1)


if __name__ == "__main__":
    main()
