"""
OTA EP reflash test suite.

Exercises the full end-to-end EP reflash path:
  test harness → DeviceClient.upload_file → HTTP upload → WP LFS
               → DeviceClient.reflash_ep  → /api/reflash/ep → FlashEp::flashUf2
               → SwdReflash → EP flash → EP reboot → swd_test_connect

Prerequisites: test_wifi must have run and stored wp_ip in context.

Run with build/.venv/bin/python3 (needs 'requests').
"""

import json
import os
import sys
import time

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
    try:
        from device_client import DeviceClient
    except ImportError as e:
        results.abort("ep_reflash",
            f"DeviceClient import failed: {e} — run with build/.venv/bin/python3")

    wp_ip = context.get("wp_ip")
    if not wp_ip:
        results.abort("ep_reflash", "no wp_ip in context — test_wifi must run first")

    # ----------------------------------------------------------------
    # ep_reflash — upload EP.uf2 then trigger reflash via HTTP API
    # ----------------------------------------------------------------

    results.start("ep_reflash")

    if not os.path.isfile(EP_UF2):
        results.abort("ep_reflash", f"EP.uf2 not found: {EP_UF2}")

    client = DeviceClient(wp_ip)

    print(f"  Uploading {os.path.basename(EP_UF2)} ({os.path.getsize(EP_UF2)//1024} KB) to {wp_ip} ...")
    ok, session_id, error = client.upload_file(EP_UF2, EP_UF2_FILENAME)
    if not ok:
        results.abort("ep_reflash", f"upload failed: {error}")

    print(f"  Triggering EP reflash ...")
    ok, error = client.reflash_ep(EP_UF2_FILENAME)
    if not ok:
        results.abort("ep_reflash", f"reflash failed: {error}")

    results.passed("ep_reflash", "upload+reflash succeeded")

    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:

        # ----------------------------------------------------------------
        # ep_runs_after — wait for EP's first UART log event after reflash.
        # epResetAndRun() clears the ep_uart_ready flag before resetting EP.
        # isr_rx32 sets it when LOGID_GEN_EP_LOG_VER arrives — the first
        # event EP emits every boot, before eclk_khz or any ECU data.
        # This is a causal signal, not a timing guess: the flag is false
        # until the UART stream is genuinely flowing.
        # ----------------------------------------------------------------

        results.start("ep_runs_after")
        deadline = time.monotonic() + SWD_TIMEOUT
        reply = ""
        while True:
            try:
                reply = vfy.command("ep_ready", timeout=5.0)
                state = json.loads(reply).get("ep_ready", {}).get("state")
                if state == "ready":
                    results.passed("ep_runs_after", reply)
                    break
            except (RttError, json.JSONDecodeError):
                pass
            if time.monotonic() >= deadline:
                results.abort("ep_runs_after",
                              f"EP UART not ready after {SWD_TIMEOUT:.0f}s: {reply}")
                break
            time.sleep(0.25)

        # ----------------------------------------------------------------
        # ep_cleanup — delete EP.uf2 from WP LFS after successful reflash
        # ----------------------------------------------------------------

        results.start("ep_cleanup")
        try:
            reply = vfy.command(f"filesystem_test_delete {EP_UF2_FILENAME}", timeout=5.0)
            state = json.loads(reply).get("filesystem_test_delete", {}).get("state")
            if state == "ok":
                results.passed("ep_cleanup", reply)
            else:
                results.failed("ep_cleanup", reply)
        except (RttError, json.JSONDecodeError) as e:
            results.failed("ep_cleanup", str(e))
