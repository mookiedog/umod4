"""
OTA WP self-reflash test suite.

Exercises the full end-to-end WP self-reflash path:
  test harness → DeviceClient.upload_file → HTTP upload → WP LFS
               → DeviceClient.reflash_wp  → /api/reflash/wp → ota_flash_task
               → FlashWp::flashUf2 → TBYB reboot → commit → cold reboot → VFY verify

Prerequisites: test_wifi must have run and stored wp_ip in context.

Run with build/.venv/bin/python3 (needs 'requests').

VFY milestone sequence emitted by WP firmware (ota_flash_task.cpp):
  {"wp_ota":"FLASH_START","file":"<path>"}   — flash programming beginning
  {"wp_ota":"FLASH_DONE","target":"0x..."}   — flash complete, about to reboot
  {"wp_ota":"TBYB_REBOOT"}                   — scheduler suspended, rom_reboot() imminent
  (WP goes dark — two reboots follow: TBYB warm boot + cold boot)
"""

import json
import os
import sys
import time

from harness.rtt import RttChannel, RttError
from harness.udp_checkin import wait_for_checkin, CheckInError

_SERVER_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "../../tools/server"))
sys.path.insert(0, _SERVER_DIR)

WP_VFY_CHANNEL = 1

_PROJ_ROOT      = os.path.normpath(os.path.join(os.path.dirname(__file__), "../.."))
WP_UF2          = os.path.join(_PROJ_ROOT, "build/WP/WP.uf2")
WP_UF2_FILENAME = "WP.uf2"

# Fallback sleep if UDP check-in is not received (TBYB warm boot ~5s + cold boot ~5s).
REBOOT_SETTLE     = 15.0
# UDP check-in and SWD reconnect each get this budget.
RECONNECT_TIMEOUT = 60.0


def run(ocd, results, context):
    try:
        from device_client import DeviceClient
    except ImportError as e:
        results.abort("wp_ota_upload",
            f"DeviceClient import failed: {e} — run with build/.venv/bin/python3")

    wp_ip = context.get("wp_ip")
    if not wp_ip:
        results.abort("wp_ota_upload", "no wp_ip in context — test_wifi must run first")

    # ----------------------------------------------------------------
    # wp_ota — pre-flight: verify WP reports OTA as available
    # ----------------------------------------------------------------

    results.start("wp_ota")
    pre_boot_slot   = None
    pre_target_slot = None
    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:
        try:
            reply = vfy.command("ota", timeout=5.0)
        except RttError as e:
            results.abort("wp_ota", str(e))
        try:
            data            = json.loads(reply)
            ota             = data.get("ota", {})
            pre_boot_slot   = ota.get("boot_slot")
            pre_target_slot = ota.get("target_slot")
            if ota.get("state") != "available":
                results.abort("wp_ota", f"OTA unavailable: {reply}")
        except (json.JSONDecodeError, AttributeError):
            results.abort("wp_ota", f"bad reply: {reply}")
    results.passed("wp_ota", reply)

    # ----------------------------------------------------------------
    # wp_ota_upload — upload WP.uf2 via HTTP
    # ----------------------------------------------------------------

    results.start("wp_ota_upload")

    if not os.path.isfile(WP_UF2):
        results.abort("wp_ota_upload", f"WP.uf2 not found: {WP_UF2}")

    client = DeviceClient(wp_ip)

    print(f"  Uploading {os.path.basename(WP_UF2)} ({os.path.getsize(WP_UF2)//1024} KB) to {wp_ip} ...")
    ok, session_id, error = client.upload_file(WP_UF2, WP_UF2_FILENAME)
    if not ok:
        results.abort("wp_ota_upload", f"upload failed: {error}")

    results.passed("wp_ota_upload", "upload succeeded")

    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:

        # ----------------------------------------------------------------
        # wp_ota_flash_start — trigger reflash, confirm OTA task started
        # ----------------------------------------------------------------

        results.start("wp_ota_flash_start")
        print(f"  Triggering WP self-reflash ...")
        ok, error = client.reflash_wp(WP_UF2_FILENAME)
        if not ok:
            results.abort("wp_ota_flash_start", f"reflash trigger failed: {error}")

        try:
            reply = vfy.wait_for('{"wp_ota":"FLASH_START"', timeout=30.0)
            results.passed("wp_ota_flash_start", reply)
        except RttError as e:
            results.abort("wp_ota_flash_start", str(e))

        # ----------------------------------------------------------------
        # wp_ota_flash_done — informational milestone: flash programming complete
        # Not aborting on timeout/lost-connection: the post-reboot boot-slot
        # check (wp_ota_verify) is the definitive success indicator.
        # FLASH_FAILED is the only result that warrants stopping here.
        # ----------------------------------------------------------------

        results.start("wp_ota_flash_done")
        rtt_alive = True
        try:
            reply = vfy.wait_for('{"wp_ota":"FLASH_', timeout=20.0)
            if "FLASH_DONE" in reply:
                results.passed("wp_ota_flash_done", reply)
            elif "FLASH_FAILED" in reply:
                results.abort("wp_ota_flash_done", reply)
            else:
                results.warn("wp_ota_flash_done", reply)
        except RttError as e:
            results.warn("wp_ota_flash_done", f"not observed (RTT): {e}")
            rtt_alive = False

        # Wait for WP's "about to reboot" RTT signal — informational only.
        if rtt_alive:
            try:
                vfy.wait_for('{"wp_ota":"TBYB_REBOOT"', timeout=10.0)
            except RttError:
                print("  (TBYB_REBOOT not seen on RTT — continuing)")

    # ----------------------------------------------------------------
    # wp_ota_checkin — device checks in via UDP after WiFi reconnect.
    # Proves the full network stack (WiFi + HTTP server) came up on the
    # new firmware.  This is a critical operational feature.
    # ----------------------------------------------------------------

    results.start("wp_ota_checkin")
    print(f"  Waiting for WP UDP check-in (up to {RECONNECT_TIMEOUT:.0f}s) ...")
    try:
        device_ip = wait_for_checkin(timeout=RECONNECT_TIMEOUT)
        results.passed("wp_ota_checkin", f"checked in from {device_ip}")
    except CheckInError as e:
        results.failed("wp_ota_checkin", str(e))
        print(f"  Falling back to {REBOOT_SETTLE:.0f}s settle wait ...")
        time.sleep(REBOOT_SETTLE)

    # ----------------------------------------------------------------
    # wp_ota_tbyb_reboot — SWD reconnect confirms device came back.
    # ----------------------------------------------------------------

    results.start("wp_ota_tbyb_reboot")
    last_err = None
    for attempt in range(3):
        print(f"  Reconnecting OpenOCD (attempt {attempt+1}/3, timeout={RECONNECT_TIMEOUT:.0f}s) ...")
        try:
            ocd.reconnect(timeout=RECONNECT_TIMEOUT)
            last_err = None
            break
        except Exception as e:
            last_err = e
            print(f"  Attempt {attempt+1} failed: {e}")
            time.sleep(2.0)
    if last_err:
        results.abort("wp_ota_tbyb_reboot", f"OpenOCD reconnect failed: {last_err}")

    results.passed("wp_ota_tbyb_reboot", "rebooted and RTT live")

    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:

        # ----------------------------------------------------------------
        # wp_ota_verify — boot + ota on rebooted firmware
        # ----------------------------------------------------------------

        results.start("wp_ota_verify")
        try:
            reply = vfy.command("boot", timeout=5.0)
            data  = json.loads(reply)
            if "boot" not in data:
                results.abort("wp_ota_verify", f"boot failed: {reply}")
        except (RttError, json.JSONDecodeError) as e:
            results.abort("wp_ota_verify", f"boot error: {e}")

        post_boot_slot = None
        try:
            reply = vfy.command("ota", timeout=5.0)
            data  = json.loads(reply)
            post_boot_slot = data.get("ota", {}).get("boot_slot")
        except (RttError, json.JSONDecodeError) as e:
            results.abort("wp_ota_verify", f"ota error: {e}")

        if pre_target_slot is not None and post_boot_slot != pre_target_slot:
            results.abort("wp_ota_verify",
                f"wrong boot slot: expected {pre_target_slot}, got {post_boot_slot}  ({reply})")
        results.passed("wp_ota_verify",
                       f"boot OK, booted slot {post_boot_slot} (was {pre_boot_slot})")

        # ----------------------------------------------------------------
        # wp_ota_cleanup — delete WP.uf2 from LFS
        # ----------------------------------------------------------------

        results.start("wp_ota_cleanup")
        try:
            reply = vfy.command(f"filesystem_test_delete {WP_UF2_FILENAME}", timeout=5.0)
            state = json.loads(reply).get("filesystem_test_delete", {}).get("state")
            if state == "ok":
                results.passed("wp_ota_cleanup", reply)
            else:
                results.failed("wp_ota_cleanup", reply)
        except (RttError, json.JSONDecodeError) as e:
            results.failed("wp_ota_cleanup", str(e))
