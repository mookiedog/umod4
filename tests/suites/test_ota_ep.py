"""
OTA EP reflash test suite.

Exercises the full end-to-end EP reflash path:
  test harness → DeviceClient.upload_file → HTTP upload → WP LFS
               → DeviceClient.reflash_ep  → /api/reflash/ep → FlashEp::flashUf2
               → SwdReflash → EP flash → EP reboot → SWD reconnect

Prerequisites: test_wifi must have run and stored wp_ip in context.

Run with build/.venv/bin/python3 (needs 'requests').
"""

import os
import sys

from harness.rtt import RttChannel, RttError

# DeviceClient lives in the server directory; add it to the path.
_SERVER_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "../../tools/server"))
sys.path.insert(0, _SERVER_DIR)

WP_VFY_CHANNEL  = 1
SWD_TIMEOUT     = 15.0

_PROJ_ROOT      = os.path.normpath(os.path.join(os.path.dirname(__file__), "../.."))
EP_UF2          = os.path.join(_PROJ_ROOT, "build/EP/EP.uf2")
EP_UF2_FILENAME = "EP.uf2"


def run(ocd, results, context):
    # Preflight: 'requests' must be importable.
    try:
        from device_client import DeviceClient
    except ImportError as e:
        results.failed("ep_reflash",
            f"DeviceClient import failed: {e} — run with build/.venv/bin/python3")
        return

    wp_ip = context.get("wp_ip")
    if not wp_ip:
        results.failed("ep_reflash", "no wp_ip in context — test_wifi must run first")
        return

    # ----------------------------------------------------------------
    # ep_reflash — upload EP.uf2 then trigger reflash via HTTP API
    # ----------------------------------------------------------------

    results.start("ep_reflash")

    if not os.path.isfile(EP_UF2):
        results.failed("ep_reflash", f"EP.uf2 not found: {EP_UF2}")
        return

    client = DeviceClient(wp_ip)

    print(f"  Uploading {os.path.basename(EP_UF2)} ({os.path.getsize(EP_UF2)//1024} KB) to {wp_ip} ...")
    ok, session_id, error = client.upload_file(EP_UF2, EP_UF2_FILENAME)
    if not ok:
        results.failed("ep_reflash", f"upload failed: {error}")
        return

    print(f"  Triggering EP reflash ...")
    ok, error = client.reflash_ep(EP_UF2_FILENAME)
    if not ok:
        results.failed("ep_reflash", f"reflash failed: {error}")
        return

    results.passed("ep_reflash", "upload+reflash succeeded")

    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:

        # ----------------------------------------------------------------
        # ep_runs_after — SWD reconnect confirms EP alive after reboot
        # FlashEp::flashUf2 reboots EP at the end of programming.
        # ----------------------------------------------------------------

        results.start("ep_runs_after")
        try:
            reply = vfy.command("swd_reconnect_after_boot", timeout=SWD_TIMEOUT)
            if "PASS" in reply:
                results.passed("ep_runs_after", reply)
            else:
                results.failed("ep_runs_after", reply)
                return
        except RttError as e:
            results.failed("ep_runs_after", str(e))
            return

        # ----------------------------------------------------------------
        # ep_cleanup — delete EP.uf2 from WP LFS after successful reflash
        # ----------------------------------------------------------------

        results.start("ep_cleanup")
        try:
            reply = vfy.command(f"lfs_delete {EP_UF2_FILENAME}", timeout=5.0)
            if "PASS" in reply:
                results.passed("ep_cleanup", reply)
            else:
                results.failed("ep_cleanup", reply)
        except RttError as e:
            results.failed("ep_cleanup", str(e))
