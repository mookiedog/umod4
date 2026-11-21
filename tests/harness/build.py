"""
Build step: bring the firmware up to date before testing.

Runs the same top-level ninja build as F7 in VS Code.  ninja knows exactly what
each artifact depends on, so a build that is already current finishes in a few
seconds without compiling anything, and a build that is out of date gets
rebuilt.  The run therefore always tests the sources in the working tree.

A failed build is fatal: there is nothing meaningful to test.
"""

import os
import subprocess
import sys
import time


def run(results, project_root):
    build_dir = os.path.join(project_root, "build")

    if not os.path.isfile(os.path.join(build_dir, "build.ninja")):
        results.fatal("ninja", "build/build.ninja not found: configure the project first "
                               "(see BUILDING.md)")

    t0 = time.monotonic()
    try:
        proc = subprocess.run(
            ["ninja", "-C", build_dir],
            capture_output=True, text=True,
        )
    except FileNotFoundError:
        results.fatal("ninja", "ninja not found on PATH")
    elapsed = time.monotonic() - t0

    if proc.returncode != 0:
        tail = "\n".join((proc.stdout + proc.stderr).splitlines()[-30:])
        print(tail, file=sys.__stdout__, flush=True)
        results.fatal("ninja", f"build failed (exit {proc.returncode}); the end of the "
                               f"build output is shown above")

    results.passed("ninja", f"build is up to date ({elapsed:.0f}s)")
