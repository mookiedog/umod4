"""
Basic VFY channel tests — verifies firmware is alive and responding.
"""

from harness.rtt import RttChannel, RttError


WP_VFY_CHANNEL = 1


def run(ocd, results):
    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:

        # ----------------------------------------------------------------
        results.start("ping")
        try:
            reply = vfy.command("ping")
            results.passed("ping") if "PASS" in reply else results.failed("ping", reply)
        except RttError as e:
            results.failed("ping", str(e))

        # ----------------------------------------------------------------
        results.start("version")
        try:
            reply = vfy.command("version")
            if "PASS" in reply and "bt=" in reply:
                results.passed("version", reply)
            else:
                results.failed("version", reply)
        except RttError as e:
            results.failed("version", str(e))

        # ----------------------------------------------------------------
        results.start("status")
        try:
            reply = vfy.command("status")
            if "PASS" in reply and "uptime_ms=" in reply:
                results.passed("status", reply)
            else:
                results.failed("status", reply)
        except RttError as e:
            results.failed("status", str(e))

        # ----------------------------------------------------------------
        results.start("lfs_test")
        try:
            reply = vfy.command("lfs_test", timeout=30.0)
            if "PASS" in reply:
                results.passed("lfs_test")
            else:
                results.failed("lfs_test", reply)
        except RttError as e:
            results.failed("lfs_test", str(e))
