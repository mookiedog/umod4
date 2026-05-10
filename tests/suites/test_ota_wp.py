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

_SERVER_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "../../tools/server"))
sys.path.insert(0, _SERVER_DIR)

WP_VFY_CHANNEL = 1

_PROJ_ROOT      = os.path.normpath(os.path.join(os.path.dirname(__file__), "../.."))
WP_UF2          = os.path.join(_PROJ_ROOT, "build/WP/WP.uf2")
WP_UF2_FILENAME = "WP.uf2"

# After TBYB_REBOOT, WP does two reboots (TBYB warm boot ~5s + cold boot ~5s).
# Wait this long before attempting OpenOCD reconnect.
REBOOT_SETTLE    = 15.0
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
    # wp_ota_status — pre-flight: verify WP reports OTA as available
    # Reports boot_slot and target_slot so partition mapping is visible
    # on failure, without having to dig into RTT ch0 printf output.
    # ----------------------------------------------------------------

    results.start("wp_ota_status")
    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:
        try:
            reply = vfy.command("ota_status", timeout=5.0)
        except RttError as e:
            results.abort("wp_ota_status", str(e))
    if '"available":0' in reply:
        results.abort("wp_ota_status", f"OTA unavailable: {reply}")
    results.passed("wp_ota_status", reply)

    # Parse boot_slot and target_slot for post-OTA verification.
    # Reply format: {"ota_status":"PASS","boot_slot":N,"target_slot":M,"available":1}
    pre_boot_slot   = None
    pre_target_slot = None
    try:
        data = json.loads(reply)
        pre_boot_slot   = data.get("boot_slot")
        pre_target_slot = data.get("target_slot")
    except (json.JSONDecodeError, AttributeError):
        pass

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

        # OTA task starts ~200ms after HTTP ack, then shuts down logger + WiFi,
        # then emits FLASH_START. Give it 30s.
        try:
            reply = vfy.wait_for('{"wp_ota":"FLASH_START"', timeout=30.0)
            results.passed("wp_ota_flash_start", reply)
        except RttError as e:
            results.abort("wp_ota_flash_start", str(e))

        # ----------------------------------------------------------------
        # wp_ota_flash_done — wait for flash programming to complete
        # Flash time for a 2.3 MB UF2 is ~30-60s; allow 120s.
        # ----------------------------------------------------------------

        results.start("wp_ota_flash_done")
        try:
            reply = vfy.wait_for('{"wp_ota":"FLASH_', timeout=120.0)
            if "FLASH_DONE" in reply:
                results.passed("wp_ota_flash_done", reply)
            else:
                # FLASH_FAILED
                results.abort("wp_ota_flash_done", reply)
        except RttError as e:
            results.abort("wp_ota_flash_done", str(e))

        # ----------------------------------------------------------------
        # wp_ota_tbyb_reboot — confirm WP is about to reboot, then
        # close VFY, wait for both reboots, reconnect OpenOCD.
        # ----------------------------------------------------------------

        results.start("wp_ota_tbyb_reboot")
        try:
            reply = vfy.wait_for('{"wp_ota":"TBYB_REBOOT"', timeout=10.0)
        except RttError as e:
            results.abort("wp_ota_tbyb_reboot", str(e))
        # VFY channel closes here as 'with' block exits

    print(f"  WP is rebooting (TBYB + cold boot), waiting {REBOOT_SETTLE:.0f}s ...")
    time.sleep(REBOOT_SETTLE)

    print(f"  Reconnecting OpenOCD (no reset, timeout={RECONNECT_TIMEOUT:.0f}s) ...")
    try:
        ocd.reconnect(timeout=RECONNECT_TIMEOUT)
    except Exception as e:
        results.abort("wp_ota_tbyb_reboot", f"OpenOCD reconnect failed: {e}")

    results.passed("wp_ota_tbyb_reboot", "rebooted and RTT live")

    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:

        # ----------------------------------------------------------------
        # wp_ota_verify — ping + version on rebooted firmware
        # ----------------------------------------------------------------

        results.start("wp_ota_verify")
        try:
            reply = vfy.command("ping", timeout=5.0)
            if "PASS" not in reply:
                results.abort("wp_ota_verify", f"ping failed: {reply}")
        except RttError as e:
            results.abort("wp_ota_verify", f"ping error: {e}")

        try:
            reply = vfy.command("version", timeout=5.0)
            if "PASS" not in reply:
                results.abort("wp_ota_verify", f"version failed: {reply}")
        except RttError as e:
            results.abort("wp_ota_verify", f"version error: {e}")

        # Confirm WP is now running from the expected slot (proves TBYB commit
        # succeeded and the new image is live, even if firmware is identical).
        try:
            reply = vfy.command("ota_status", timeout=5.0)
        except RttError as e:
            results.abort("wp_ota_verify", f"ota_status error: {e}")
        post_boot_slot = None
        try:
            data = json.loads(reply)
            post_boot_slot = data.get("boot_slot")
        except (json.JSONDecodeError, AttributeError):
            pass
        if pre_target_slot is not None and post_boot_slot != pre_target_slot:
            results.abort("wp_ota_verify",
                f"wrong boot slot: expected {pre_target_slot}, got {post_boot_slot}  ({reply})")
        results.passed("wp_ota_verify", f"version OK, booted slot {post_boot_slot} (was {pre_boot_slot})")

        # ----------------------------------------------------------------
        # wp_ota_cleanup — delete WP.uf2 from LFS
        # ----------------------------------------------------------------

        results.start("wp_ota_cleanup")
        try:
            reply = vfy.command(f"lfs_delete {WP_UF2_FILENAME}", timeout=5.0)
            if "PASS" in reply:
                results.passed("wp_ota_cleanup", reply)
            else:
                results.failed("wp_ota_cleanup", reply)
        except RttError as e:
            results.failed("wp_ota_cleanup", str(e))
