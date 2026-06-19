"""
Basic VFY channel tests — verifies firmware is alive and responding.
"""

import json

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
        results.start("boot")
        try:
            data = _cmd(vfy, "boot")
            b = data.get("boot", {})
            if "slot" in b and "built" in b:
                results.passed("boot", f"slot={b['slot']} built={b['built']}")
            else:
                results.failed("boot", f"missing keys: {data}")
        except (RttError, ValueError) as e:
            results.failed("boot", str(e))

        # ----------------------------------------------------------------
        results.start("heap")
        try:
            data = _cmd(vfy, "heap")
            h = data.get("heap", {})
            if "remaining" in h:
                results.passed("heap", f"remaining={h['remaining']} free={h.get('free','?')}")
            else:
                results.failed("heap", f"missing keys: {data}")
        except (RttError, ValueError) as e:
            results.failed("heap", str(e))

        # ----------------------------------------------------------------
        results.start("sd")
        try:
            data = _cmd(vfy, "sd")
            s = data.get("sd", {})
            sd_state = s.get("state", "")
            if sd_state == "operational":
                results.passed("sd", f"size_mb={s.get('size_mb','?')}")
            else:
                results.fatal("sd", f"SD card not present or not operational (state={sd_state}) — insert card and retry")
        except (RttError, ValueError) as e:
            results.fatal("sd", str(e))

        # ----------------------------------------------------------------
        results.start("filesystem_test_rw")
        try:
            data = _cmd(vfy, "filesystem_test_rw", timeout=30.0)
            state = data.get("filesystem_test_rw", {}).get("state", "")
            if state == "ok":
                results.passed("filesystem_test_rw")
            else:
                results.fatal("filesystem_test_rw", f"state={state}")
        except (RttError, ValueError) as e:
            results.fatal("filesystem_test_rw", str(e))

        # ----------------------------------------------------------------
        results.start("logstore_fsck")
        try:
            data = _cmd(vfy, "logstore_fsck", timeout=30.0)
            fsck = data.get("logstore_fsck", {})
            state = fsck.get("state", "")
            errors = fsck.get("errors", -1)
            if state == "pass" and errors == 0:
                results.passed("logstore_fsck")
            else:
                results.fatal("logstore_fsck", f"state={state} errors={errors}")
        except (RttError, ValueError) as e:
            results.fatal("logstore_fsck", str(e))
