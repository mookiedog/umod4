"""
Provisioning test suite.

Brings a umod4 board from zero state (blank or any prior contents) to
a fully provisioned state connected to the home WiFi network.

Steps:
  1. Erase and reflash WP (via SWD BOOTSEL + flash_WP -e)
  2. Verify WP starts in AP mode (RTT ap_status command)
  3. Connect ap_proxy to WP's AP network
  4. POST WiFi credentials to WP via HTTP (/api/config)
  5. Wait for WP to reboot and connect to the home WiFi network

Prerequisites:
  - CMSIS-DAP debug probe connected
  - ap_proxy USB device connected
  - --ssid and --device-name args provided to runner
  - OpenOCD is running when this suite is invoked
"""

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
# After fresh flash, WP does a TBYB warm-boot then cold-boot (~5s each).
# This also covers the time needed to start AP mode.
POST_FLASH_SETTLE = 10.0

# After POSTing /api/config, WP saves to flash and reboots (~3s shutdown +
# ~3s cold-boot).  Reconnect OCD after this grace period.
POST_CONFIG_SETTLE = 8.0

# How long to poll wifi_status after config reboot before giving up.
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
        msg = f"wp_enter_bootsel failed (exit {r.returncode}): {r.stderr.strip()}"
        if "unable to find a matching CMSIS-DAP device" in r.stderr:
            results.fatal("prov_enter_bootsel", msg)
        results.abort("prov_enter_bootsel", msg)

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
        results.abort("prov_flash_wp",
            f"flash_WP -e failed (exit {r.returncode}): {r.stderr.strip()}")
    results.passed("prov_flash_wp")

    # ----------------------------------------------------------------
    # prov_ocd_restart — restart OpenOCD and wait for RTT
    # flash_WP ends with 'picotool load -p 1' which boots in TBYB mode;
    # POST_FLASH_SETTLE lets both warm-boot and cold-boot complete.
    # ----------------------------------------------------------------

    results.start("prov_ocd_restart")
    print("  Starting OpenOCD...")
    try:
        ocd.start(reset=True)
        ocd.wait_ready()
    except (OpenOCDError, OSError) as e:
        results.abort("prov_ocd_restart", f"OpenOCD failed: {e}")

    print(f"  Waiting {POST_FLASH_SETTLE:.0f}s for WP boot...")
    time.sleep(POST_FLASH_SETTLE)
    results.passed("prov_ocd_restart", "RTT live")

    # ----------------------------------------------------------------
    # prov_ap_boot — verify WP reports AP mode via VFY RTT
    # ----------------------------------------------------------------

    results.start("prov_ap_boot")
    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:
        try:
            reply = vfy.command("ap_status", timeout=10.0)
            if "PASS" not in reply:
                results.abort("prov_ap_boot", f"WP not in AP mode: {reply}")
            results.passed("prov_ap_boot", reply)
        except RttError as e:
            results.abort("prov_ap_boot", str(e))

    # ----------------------------------------------------------------
    # prov_ap_connect — ap_proxy scans for umod4_XXXX and connects
    # Default AP password = SSID (device name).
    # ----------------------------------------------------------------

    results.start("prov_ap_connect")
    port = find_port()
    if port is None:
        results.abort("prov_ap_connect",
            "ap_proxy serial port not found — is the device attached?")

    print(f"  Opening ap_proxy on {port} ...")
    with ApProxy(port) as proxy:
        if not proxy.ping():
            results.abort("prov_ap_connect", "ap_proxy not responding to PING")
        try:
            ip = proxy.find_and_connect()
            context["ap_proxy_port"] = port
            results.passed("prov_ap_connect", f"connected  proxy_ip={ip}")
        except ApProxyError as e:
            results.abort("prov_ap_connect", str(e))

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

        print(f"  POSTing WiFi config (ssid={context['ssid']}, "
              f"device={context['device_name']}) ...")
        try:
            code, body = proxy.post("/api/config", config_body)
            if code != 200:
                results.abort("prov_set_wifi",
                    f"POST /api/config returned HTTP {code}: {body}")
            results.passed("prov_set_wifi", "config accepted — WP rebooting")
        except ApProxyError as e:
            results.abort("prov_set_wifi", str(e))

    # ----------------------------------------------------------------
    # prov_wait_reboot — wait for WP to save config and reboot
    # After the POST, WP shuts down WiFi (~3s), saves to flash, reboots.
    # ----------------------------------------------------------------

    results.start("prov_wait_reboot")
    print(f"  Waiting {POST_CONFIG_SETTLE:.0f}s for WP config-save reboot...")
    time.sleep(POST_CONFIG_SETTLE)
    try:
        ocd.reconnect()
    except (OpenOCDError, OSError) as e:
        results.abort("prov_wait_reboot", f"OpenOCD reconnect failed: {e}")
    results.passed("prov_wait_reboot", "RTT live after reboot")

    # ----------------------------------------------------------------
    # prov_wifi_connect — poll RTT wifi_status until WP joins home WiFi
    # ----------------------------------------------------------------

    results.start("prov_wifi_connect")
    deadline = time.monotonic() + WIFI_CONNECT_WAIT
    with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:
        while True:
            try:
                reply = vfy.command("wifi_status", timeout=10.0)
            except RttError as e:
                results.abort("prov_wifi_connect", str(e))
            if "PASS" in reply:
                break
            if "not_connected" not in reply or time.monotonic() >= deadline:
                results.abort("prov_wifi_connect",
                    reply + f" — WiFi did not connect within {WIFI_CONNECT_WAIT:.0f}s")
            time.sleep(2.0)
        results.passed("prov_wifi_connect", reply)
