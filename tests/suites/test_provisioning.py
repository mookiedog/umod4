"""
Provisioning test suite.

Brings a umod4 board from zero state (blank or any prior contents) to
a fully provisioned state connected to the home WiFi network.

Steps:
  1. Erase and reflash WP (via SWD BOOTSEL + flash_WP -e)
  2. Verify WP starts in AP mode (RTT wifi command)
  3. Connect ap_proxy to WP's AP network
  4. POST WiFi credentials to WP via HTTP (/api/config)
  5. Wait for WP to reboot and connect to the home WiFi network

Prerequisites:
  - CMSIS-DAP debug probe connected
  - ap_proxy USB device connected
  - --ssid and --device-name args provided to runner
  - OpenOCD is running when this suite is invoked
"""

import json
import os
import subprocess
import time

from harness.rtt import RttChannel, RttError
from harness.ap_proxy import ApProxy, ApProxyError, find_port
from harness.openocd import OpenOCDError
from harness.usbipd import ensure_attached
from harness.usb_ids import AP_PROXY_HW_ID


WP_VFY_CHANNEL = 1

_PROJ_ROOT         = os.path.normpath(os.path.join(os.path.dirname(__file__), "../.."))
WP_ENTER_BOOTSEL   = os.path.join(_PROJ_ROOT, "tools", "wp_enter_bootsel")
WP_USB_BOOT_BINARY = os.path.join(_PROJ_ROOT, "build", "WpUsbBoot", "WpUsbBoot")
FLASH_WP           = os.path.join(_PROJ_ROOT, "tools", "flash_WP")
WP_BUILD_DIR       = os.path.join(_PROJ_ROOT, "build", "WP")

# How long to poll wifi after config reboot before giving up.
WIFI_CONNECT_WAIT = 60.0


def run(ocd, results, context):
    try:
        ensure_attached(AP_PROXY_HW_ID)
    except RuntimeError as e:
        results.abort("ap_proxy_attach", str(e))
        return

    try:
        _run(ocd, results, context)
    finally:
        # Always leave OpenOCD running for subsequent suites.
        # This suite stops OCD early (to free SWD) and normally restarts it
        # at prov_ocd_restart.  If we aborted before that, restart here so
        # later suites are not stranded with a dead connection.
        if not ocd.is_running:
            try:
                ocd.start(reset=True)
                ocd.wait_ready()
            except (OpenOCDError, OSError):
                pass   # subsequent suites will fail on their own


def _run(ocd, results, context):

    # ----------------------------------------------------------------
    # prov_enter_bootsel — stop OpenOCD and enter BOOTSEL mode via SWD
    # OpenOCD must be stopped before wp_enter_bootsel uses SWD.
    # Do NOT call picotool erase here: on WSL2 the RP2350 reappears on
    # the Windows USB bus after the reboot and is not yet attached to WSL2.
    # flash_WP -e handles the usbipd re-attach before erasing.
    # ----------------------------------------------------------------

    results.start("prov_enter_bootsel")
    print("  Stopping OpenOCD to free SWD...")
    ocd.stop()

    print("  Entering WP BOOTSEL mode via SWD...")
    r = subprocess.run([WP_ENTER_BOOTSEL, WP_USB_BOOT_BINARY],
                       capture_output=True, text=True)
    if r.returncode != 0:
        detail = r.stderr.strip() or r.stdout.strip()
        print("  NOTE: ensure that the WP is connected to the test PC via USB cable")
        results.fatal("prov_enter_bootsel",
            f"wp_enter_bootsel failed (exit {r.returncode}): {detail}")

    results.passed("prov_enter_bootsel")

    # ----------------------------------------------------------------
    # prov_flash_wp — erase + flash via flash_WP -e
    # flash_WP handles: usbipd attach on WSL2, erase, partition table,
    # WP image, and final reboot.
    # ----------------------------------------------------------------

    results.start("prov_flash_wp")
    print(f"  Erasing and flashing WP from {WP_BUILD_DIR} ...")
    r = subprocess.run([FLASH_WP, "-e", WP_BUILD_DIR], capture_output=True, text=True)
    if r.returncode != 0:
        detail = r.stderr.strip() or r.stdout.strip()
        results.fatal("prov_flash_wp",
            f"flash_WP -e failed (exit {r.returncode}): {detail}")
    results.passed("prov_flash_wp")

    # ----------------------------------------------------------------
    # prov_ocd_restart — restart OpenOCD and wait for RTT.
    # flash_WP ends with a plain 'picotool reboot' — no TBYB pending state,
    # so this is a single cold boot.  ocd.wait_ready() fires ~200ms into
    # boot; prov_ap_boot's wifi command waits for VfyTask + AP mode (~5.5s).
    # ----------------------------------------------------------------

    results.start("prov_ocd_restart")
    print("  Starting OpenOCD...")
    try:
        ocd.start(reset=True)
        ocd.wait_ready()
    except (OpenOCDError, OSError) as e:
        results.fatal("prov_ocd_restart", f"OpenOCD failed: {e}")

    results.passed("prov_ocd_restart", "RTT live")

    # ----------------------------------------------------------------
    # prov_ap_boot — poll RTT wifi until WP enters AP mode.
    # not_connected is acceptable while WiFiManager is still initializing;
    # any other state is unexpected.
    # ----------------------------------------------------------------

    AP_BOOT_WAIT = 15.0
    results.start("prov_ap_boot")
    deadline = time.monotonic() + AP_BOOT_WAIT
    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:
        while True:
            try:
                reply = vfy.command("wifi", timeout=5.0)
            except RttError:
                if time.monotonic() >= deadline:
                    results.fatal("prov_ap_boot",
                        f"VFY channel not responding after {AP_BOOT_WAIT:.0f}s")
                time.sleep(1.0)
                continue
            try:
                state = json.loads(reply).get("wifi", {}).get("state", "")
            except (json.JSONDecodeError, AttributeError):
                state = ""
            if state == "ap_mode":
                break
            if state != "not_connected" or time.monotonic() >= deadline:
                results.fatal("prov_ap_boot",
                    f"WP not in AP mode after {AP_BOOT_WAIT:.0f}s: {reply}")
            time.sleep(1.0)
        results.passed("prov_ap_boot", reply)

    # ----------------------------------------------------------------
    # prov_ap_connect — ap_proxy scans for umod4_XXXX and connects
    # Default AP password = SSID (device name).
    # ----------------------------------------------------------------

    results.start("prov_ap_connect")
    port = find_port()
    if port is None:
        results.fatal("prov_ap_connect",
            "ap_proxy serial port not found — is the device attached?")

    print(f"  Opening ap_proxy on {port} ...")
    with ApProxy(port) as proxy:
        if not proxy.ping():
            results.fatal("prov_ap_connect", "ap_proxy not responding to PING")
        try:
            ip = proxy.find_and_connect()
            context["ap_proxy_port"] = port
            results.passed("prov_ap_connect", f"connected  proxy_ip={ip}")
        except ApProxyError as e:
            results.fatal("prov_ap_connect", str(e))

        # ----------------------------------------------------------------
        # prov_set_wifi — POST WiFi credentials to WP via HTTP
        # Proxy is still connected to WP's AP; send credentials to /api/config.
        # WP will save to flash and reboot; the AP disappears immediately after.
        # ----------------------------------------------------------------

        results.start("prov_set_wifi")
        config_body = {
            "wifi_ssid":    context["ssid"],
            "device_name":  context["device_name"],
        }
        if context.get("password"):
            config_body["wifi_password"] = context["password"]
        if context.get("ap_ssid"):
            config_body["ap_ssid"] = context["ap_ssid"]
        if context.get("ap_password"):
            config_body["ap_password"] = context["ap_password"]

        print(f"  POSTing WiFi config (ssid={context['ssid']}, "
              f"device={context['device_name']}) ...")
        try:
            code, body = proxy.post("/api/config", config_body)
            if code != 200:
                results.fatal("prov_set_wifi",
                    f"POST /api/config returned HTTP {code}: {body}")
            results.passed("prov_set_wifi", "config accepted — WP rebooting")
        except ApProxyError as e:
            results.fatal("prov_set_wifi", str(e))

    # ----------------------------------------------------------------
    # prov_wait_reboot — wait for WP to reboot after config save.
    # Detect the reboot by polling the VFY channel until it stops responding
    # (RTT drops when WP reboots), then reconnect OpenOCD and wait for the
    # new boot to come up.  This is more reliable than catching a one-shot
    # CONFIG_REBOOT message that fires just before the watchdog trips —
    # RTT does not buffer for clients that connect after the message is sent.
    # ----------------------------------------------------------------

    results.start("prov_wait_reboot")
    print("  Waiting for reboot (watching RTT drop)...")
    deadline = time.monotonic() + 20.0
    while time.monotonic() < deadline:
        try:
            with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL), connect_timeout=1.0) as vfy:
                vfy.command("heap", timeout=2.0)
            time.sleep(0.2)
        except Exception:
            break
    else:
        results.fatal("prov_wait_reboot", "Timeout waiting for WP to reboot (RTT never dropped)")

    print("  Reconnecting for cold boot...")
    try:
        ocd.reconnect(timeout=20.0)
    except (OpenOCDError, OSError) as e:
        results.fatal("prov_wait_reboot", f"OpenOCD reconnect failed: {e}")
    results.passed("prov_wait_reboot", "RTT live after reboot")

    # ----------------------------------------------------------------
    # prov_wifi_connect — poll RTT wifi until WP joins home WiFi
    # ----------------------------------------------------------------

    results.start("prov_wifi_connect")
    deadline = time.monotonic() + WIFI_CONNECT_WAIT
    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:
        while True:
            try:
                reply = vfy.command("wifi", timeout=5.0)
            except RttError as e:
                results.fatal("prov_wifi_connect", str(e))
            try:
                state = json.loads(reply).get("wifi", {}).get("state", "")
            except (json.JSONDecodeError, AttributeError):
                state = ""
            if state == "connected":
                break
            if state != "not_connected" or time.monotonic() >= deadline:
                results.fatal("prov_wifi_connect",
                    reply + f" — WiFi did not connect within {WIFI_CONNECT_WAIT:.0f}s")
            time.sleep(2.0)
        results.passed("prov_wifi_connect", reply)
