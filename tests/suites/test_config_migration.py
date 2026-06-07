"""
Config migration test suite.

Verifies that the WP firmware handles all flash config states correctly:

  1. cfg_invalid_magic  — wrong magic → clean defaults → valid AP SSID
  2. cfg_bad_crc        — correct magic, bad CRC → clean defaults → valid AP SSID
  3. cfg_valid_v0       — valid v0 config with WiFi credentials → device connects

Each test injects a synthetic config blob via OpenOCD, reboots the device,
and verifies the outcome via RTT.  The firmware is flashed once at the start
of the suite to guarantee a known state.

Prerequisites:
  - CMSIS-DAP debug probe connected
  - --ssid, --password, and --device-name args provided to runner
  - OpenOCD is running when this suite is invoked

Adding a migration test for a new config version (e.g. v1):
  1. Add make_valid_v1() to tests/harness/flash_config_blobs.py
  2. Add a cfg_valid_v0_migrate_to_v1 step below that injects a v0 blob
     and verifies the device boots correctly with the migrated config.
"""

import json
import os
import re
import time

from harness.rtt import RttChannel, RttError
from harness.flash_ops import Record, PROC_WP, WP_FLASH_BASE, WP_CONFIG_OFFSET, restore_wp_config
from harness.flash_config_blobs import make_blank, make_invalid_magic, make_bad_crc, make_valid_v0
from harness.openocd import OpenOCDError

WP_VFY_CHANNEL  = 1
AP_SSID_PATTERN = re.compile(r'^umod4_[0-9A-Fa-f]{4}$')

_PROJ_ROOT         = os.path.normpath(os.path.join(os.path.dirname(__file__), "../.."))
WP_ENTER_BOOTSEL   = os.path.join(_PROJ_ROOT, "tools", "wp_enter_bootsel")
WP_USB_BOOT_BINARY = os.path.join(_PROJ_ROOT, "build", "WpUsbBoot", "WpUsbBoot")
FLASH_WP           = os.path.join(_PROJ_ROOT, "tools", "flash_WP")
WP_BUILD_DIR       = os.path.join(_PROJ_ROOT, "build", "WP")

POST_FLASH_SETTLE  = 10.0   # seconds after flashing before RTT is live
BOOT_SETTLE        = 8.0    # seconds after config inject + reset before checking RTT
WIFI_CONNECT_TIMEOUT = 30.0 # seconds to wait for STA connect in cfg_valid_v0


def _inject_config(ocd, blob: bytes) -> None:
    """Write a 512-byte config blob to the WP config partition and reboot.

    Stops the running OpenOCD, uses restore_wp_config() (which spawns its own
    OpenOCD instance to program flash and reset the device), then restarts
    the persistent OpenOCD session without issuing another reset.
    """
    record = Record(PROC_WP, WP_FLASH_BASE + WP_CONFIG_OFFSET, blob)
    ocd.stop()
    restore_wp_config([record])          # programs flash, issues reset run
    time.sleep(BOOT_SETTLE)
    ocd.start(reset=False)               # reconnect; device already rebooting
    ocd.wait_ready()


def _wifi_state(ocd, timeout: float = 15.0) -> dict:
    """Query WP wifi state via RTT. Returns the 'wifi' dict."""
    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:
        reply = vfy.command("wifi", timeout=timeout)
        return json.loads(reply).get("wifi", {})


def _wait_wifi_connected(ocd, timeout: float = WIFI_CONNECT_TIMEOUT) -> dict:
    """Poll RTT until wifi state is 'connected' or timeout expires."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            state = _wifi_state(ocd, timeout=5.0)
            if state.get("state") == "connected":
                return state
        except (RttError, json.JSONDecodeError):
            pass
        time.sleep(2.0)
    raise TimeoutError(f"WiFi did not connect within {timeout:.0f}s")


def run(ocd, results, context):
    import subprocess

    # ----------------------------------------------------------------
    # Flash current firmware — ensures we are testing the right code.
    # ----------------------------------------------------------------

    results.start("cfg_flash_wp")
    print("  Stopping OpenOCD to free SWD...")
    ocd.stop()
    print("  Entering WP BOOTSEL mode via SWD...")
    r = subprocess.run([WP_ENTER_BOOTSEL, WP_USB_BOOT_BINARY],
                       capture_output=True, text=True)
    if r.returncode != 0:
        results.fatal("cfg_flash_wp",
            f"wp_enter_bootsel failed (exit {r.returncode}): "
            f"{r.stderr.strip() or r.stdout.strip()}")
    print(f"  Flashing WP from {WP_BUILD_DIR} ...")
    r = subprocess.run([FLASH_WP, "-e", WP_BUILD_DIR], capture_output=True, text=True)
    if r.returncode != 0:
        results.fatal("cfg_flash_wp",
            f"flash_WP -e failed (exit {r.returncode}): "
            f"{r.stderr.strip() or r.stdout.strip()}")
    print(f"  Waiting {POST_FLASH_SETTLE:.0f}s for WP boot...")
    time.sleep(POST_FLASH_SETTLE)
    ocd.start(reset=False)
    ocd.wait_ready()
    results.passed("cfg_flash_wp")

    # ----------------------------------------------------------------
    # Test 1: invalid magic
    # Inject a blob with wrong magic.  Firmware must apply clean defaults
    # and boot into AP mode with a valid auto-generated SSID.
    # ----------------------------------------------------------------

    results.start("cfg_invalid_magic")
    print("  Injecting config with invalid magic...")
    try:
        _inject_config(ocd, make_invalid_magic(
            wifi_ssid="garbage_ssid", wifi_password="garbage_pw",
            ap_ssid="garbage_ap",    ap_password="garbage_ap_pw"))
        state = _wifi_state(ocd)
        if state.get("state") != "ap_mode":
            results.failed("cfg_invalid_magic",
                f"Expected ap_mode, got: {state}")
        else:
            ssid = state.get("ssid", "")
            if not AP_SSID_PATTERN.match(ssid):
                results.failed("cfg_invalid_magic",
                    f"AP SSID '{ssid}' does not match umod4_XXXX pattern")
            else:
                results.passed("cfg_invalid_magic", f"ssid={ssid}")
    except Exception as e:
        results.failed("cfg_invalid_magic", str(e))

    # ----------------------------------------------------------------
    # Test 2: correct magic, bad CRC
    # Inject a blob with correct magic but corrupted CRC.  Firmware must
    # apply clean defaults (not attempt field salvage).
    # ----------------------------------------------------------------

    results.start("cfg_bad_crc")
    print("  Injecting config with bad CRC...")
    try:
        _inject_config(ocd, make_bad_crc(
            wifi_ssid="garbage_ssid", wifi_password="garbage_pw",
            ap_ssid="garbage_ap",    ap_password="garbage_ap_pw"))
        state = _wifi_state(ocd)
        if state.get("state") != "ap_mode":
            results.failed("cfg_bad_crc",
                f"Expected ap_mode, got: {state}")
        else:
            ssid = state.get("ssid", "")
            if not AP_SSID_PATTERN.match(ssid):
                results.failed("cfg_bad_crc",
                    f"AP SSID '{ssid}' does not match umod4_XXXX pattern")
            else:
                results.passed("cfg_bad_crc", f"ssid={ssid}")
    except Exception as e:
        results.failed("cfg_bad_crc", str(e))

    # ----------------------------------------------------------------
    # Test 3: valid v0 config with real WiFi credentials
    # Inject a correctly formed v0 config.  Firmware must load it and
    # connect to the home network.
    # ----------------------------------------------------------------

    results.start("cfg_valid_v0")
    ssid     = context.get("ssid", "")
    password = context.get("password", "")
    name     = context.get("device_name", "test-device")
    if not ssid:
        results.failed("cfg_valid_v0", "No --ssid provided; cannot test STA connect")
    else:
        print(f"  Injecting valid v0 config (ssid={ssid}, device={name})...")
        try:
            _inject_config(ocd, make_valid_v0(
                wifi_ssid=ssid, wifi_password=password, device_name=name))
            print(f"  Waiting up to {WIFI_CONNECT_TIMEOUT:.0f}s for WiFi connect...")
            state = _wait_wifi_connected(ocd)
            ip = state.get("ip", "")
            results.passed("cfg_valid_v0",
                f"connected ssid={state.get('ssid')} ip={ip}")
        except TimeoutError as e:
            results.failed("cfg_valid_v0", str(e))
        except Exception as e:
            results.failed("cfg_valid_v0", str(e))
