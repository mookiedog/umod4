"""
usbipd helpers for WSL USB device management.

All functions are no-ops on non-WSL systems (detected via /proc/version).
On WSL, usbipd.exe is called via Windows interop.

Note: if multiple devices share the same VID:PID, usbipd_state() matches
the first one found in 'usbipd list' output.  The harness assumes one
debug probe and one ap_proxy are connected at a time.
"""

import re
import subprocess
import sys
import os
import time


def _is_wsl():
    try:
        with open("/proc/version") as f:
            return "microsoft" in f.read().lower()
    except OSError:
        return False


IS_WSL = _is_wsl()


def usbipd_state(hardware_id):
    """
    Return the usbipd state string for the first device matching hardware_id,
    or None if not found.

    Possible return values: 'Not shared', 'Shared', 'Attached'
    """
    if not IS_WSL:
        return None
    result = subprocess.run(
        ["usbipd.exe", "list"],
        capture_output=True, text=True
    )
    for line in result.stdout.splitlines():
        if hardware_id.lower() in line.lower():
            m = re.search(r"(Not shared|Shared|Attached)", line, re.IGNORECASE)
            if m:
                return m.group(1)
    return None


def _linux_has_usb(hardware_id):
    """Return True if hardware_id (VID:PID) is enumerated in the Linux USB subsystem."""
    vid, pid = hardware_id.lower().split(":")
    base = "/sys/bus/usb/devices"
    try:
        for name in os.listdir(base):
            dev = os.path.join(base, name)
            try:
                v = open(os.path.join(dev, "idVendor")).read().strip()
                p = open(os.path.join(dev, "idProduct")).read().strip()
                if v == vid and p == pid:
                    return True
            except OSError:
                pass
    except OSError:
        pass
    return False


def ensure_attached(hardware_id, linux_enum_timeout=10.0):
    """
    Attach hardware_id to WSL if not already attached.
    Raises RuntimeError if the device is not found, not bound, or attach fails.

    After a fresh attach, polls /sys/bus/usb/devices until the device is
    enumerated on the Linux side.  usbipd reports 'Attached' as soon as the
    Windows-side handoff completes, but libusb cannot see the device until
    Linux finishes enumeration — which takes an unpredictable extra moment.
    Skips the enumeration wait if the device was already Attached on entry.
    """
    if not IS_WSL:
        return
    state = usbipd_state(hardware_id)
    if state == "Attached":
        if _linux_has_usb(hardware_id):
            return   # genuinely attached and visible in Linux
        # usbipd says Attached but Linux can't see it (stale after
        # sleep/resume or WSL kernel hiccup). Detach and reattach.
        subprocess.run(
            ["usbipd.exe", "detach", "--hardware-id", hardware_id],
            capture_output=True, text=True
        )
        time.sleep(1.0)
        state = usbipd_state(hardware_id)
    if state is None:
        raise RuntimeError(
            f"USB device {hardware_id} not found — is it plugged in?"
        )
    if state == "Not shared":
        raise RuntimeError(
            f"USB device {hardware_id} is not bound. "
            f"Run tools/setup_usb_wsl.ps1 as Administrator, then unplug and replug the device."
        )
    result = subprocess.run(
        ["usbipd.exe", "attach", "--hardware-id", hardware_id, "--wsl"],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"usbipd attach {hardware_id} failed: {result.stderr.strip()}"
        )
    state = usbipd_state(hardware_id)
    if state != "Attached":
        raise RuntimeError(
            f"usbipd attach {hardware_id} completed but state is '{state}', expected 'Attached'"
        )
    # usbipd 'Attached' means Windows handed off the device, but Linux
    # enumeration is still in progress.  Poll until libusb can see it.
    deadline = time.monotonic() + linux_enum_timeout
    while not _linux_has_usb(hardware_id) and time.monotonic() < deadline:
        time.sleep(0.2)
    if not _linux_has_usb(hardware_id):
        raise RuntimeError(
            f"USB device {hardware_id} attached to WSL but did not enumerate in Linux "
            f"after {linux_enum_timeout:.0f}s"
        )


def ensure_detached(hardware_id):
    """
    Detach hardware_id from WSL if currently attached.  No-op if not attached.
    Errors are printed but not raised — detach is best-effort cleanup.
    """
    if not IS_WSL:
        return
    if usbipd_state(hardware_id) != "Attached":
        return
    result = subprocess.run(
        ["usbipd.exe", "detach", "--hardware-id", hardware_id],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"  warn: usbipd detach {hardware_id} failed: {result.stderr.strip()}",
              file=sys.stderr)
