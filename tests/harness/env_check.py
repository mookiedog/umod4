"""
Environment check: confirm the build environment is sound before building anything.

Runs scripts/check_build_env, which checks the toolchains, the source checkouts and the other
prerequisites for building umod4.  A missing toolchain would otherwise show up as a long
compiler error halfway through the build; this reports it first, with the BUILDING.md section
that explains the fix.

The script exits 0 if everything is present and 1 if anything is missing or wrong.  A problem
is fatal: the machine cannot build, so there is nothing meaningful to test.
"""

import os
import subprocess
import sys


def run(results, project_root):
    script = os.path.join(project_root, "scripts", "check_build_env")

    if not os.path.isfile(script):
        results.fatal("check_build_env", "scripts/check_build_env not found")

    try:
        proc = subprocess.run(
            [script], capture_output=True, text=True, timeout=10,
        )
    except subprocess.TimeoutExpired:
        results.fatal("check_build_env", "scripts/check_build_env did not finish within 10 s")

    output = (proc.stdout + proc.stderr).rstrip()

    if proc.returncode != 0:
        # The script prints only the problems, each with its BUILDING.md section.
        print(output, file=sys.__stdout__, flush=True)
        results.fatal("check_build_env", "the build environment has problems; they are listed above")

    # On success the script prints one summary line, such as "all 28 checks passed".
    summary = output.splitlines()[-1] if output else ""
    results.passed("check_build_env", summary.replace("check_build_env: ", ""))
