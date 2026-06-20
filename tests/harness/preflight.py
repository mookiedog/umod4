"""
Pre-flight checks for the umod4 test runner.

Checks run in order; each calls results.fatal() on hard failure (stops the
entire run) or results.passed() on success.  server_kill always passes, with a
warning detail if it had to kill the process.

args_wifi / args_device_name: only enforced when the provisioning suite is
included in the run (suite name starts with "test_provisioning").

WSL USB attachment is NOT performed here.  Preflight only verifies that
required devices are visible on the USB bus.  The runner attaches the debug
probe after preflight; test_provisioning attaches the ap_proxy when needed.
"""

import os
import shutil
import subprocess

from harness.usbipd import IS_WSL, usbipd_state
from harness.usb_ids import AP_PROXY_HW_ID


AP_PROXY_VID_PID  = AP_PROXY_HW_ID
CMSIS_DAP_VID_PID = "2e8a:000c"


# ── Helpers ────────────────────────────────────────────────────────────────

def _linux_has_usb(vid_pid):
    """Return True if the device appears under /sys/bus/usb/devices/."""
    vid, pid = vid_pid.lower().split(":")
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


def _check_device_wsl(results, name, vid_pid, label):
    """Verify device is visible in usbipd list (any state).  Does not attach."""
    state = usbipd_state(vid_pid)
    if state is None:
        results.fatal(name,
            f"{label} ({vid_pid}) not found — is it plugged in and bound?")
    else:
        results.passed(name, f"{label} ({vid_pid}) present (state: {state})")


def _check_device_linux(results, name, vid_pid, label):
    if _linux_has_usb(vid_pid):
        results.passed(name, f"{label} ({vid_pid}) found")
    else:
        results.fatal(name, f"{label} ({vid_pid}) not found — is it plugged in?")


# ── Individual checks ──────────────────────────────────────────────────────

def check_server_kill(results):
    r = subprocess.run(["pgrep", "-f", "umod4_server.py"], capture_output=True)
    if r.returncode != 0:
        results.passed("server_kill")
        return
    subprocess.run(["pkill", "-f", "umod4_server.py"])
    results.passed("server_kill", "server was running — killed")


def check_ap_proxy(results):
    if IS_WSL:
        _check_device_wsl(results, "ap_proxy_usb", AP_PROXY_VID_PID, "ap_proxy")
    else:
        _check_device_linux(results, "ap_proxy_usb", AP_PROXY_VID_PID, "ap_proxy")


def check_cmsis_dap(results):
    if IS_WSL:
        _check_device_wsl(results, "cmsis_dap_usb", CMSIS_DAP_VID_PID, "CMSIS-DAP")
    else:
        _check_device_linux(results, "cmsis_dap_usb", CMSIS_DAP_VID_PID, "CMSIS-DAP")


def check_tool(results, name, cmd, *version_args):
    path = shutil.which(cmd)
    if path is None:
        results.fatal(name, f"{cmd} not found on PATH")
        return
    r = subprocess.run([cmd] + list(version_args), capture_output=True, text=True)
    if r.returncode != 0:
        results.fatal(name,
            f"{cmd} {' '.join(version_args)} failed (exit {r.returncode})")
        return
    first = (r.stdout or r.stderr or "").splitlines()[0].strip()
    results.passed(name, first)


def check_build_artifact(results, name, path, label):
    if os.path.isfile(path):
        results.passed(name)
    else:
        results.fatal(name, f"{label} not found: {path}")


def check_probe_not_busy(results):
    """Try to open the CMSIS-DAP probe briefly. If a debug session holds it,
    OpenOCD fails fast with a recognizable error."""
    r = subprocess.run(
        ["openocd",
         "-f", "interface/cmsis-dap.cfg",
         "-f", "target/rp2350.cfg",
         "-c", "adapter speed 20000",
         "-c", "init",
         "-c", "shutdown"],
        capture_output=True, text=True, timeout=10)
    output = r.stdout + r.stderr
    if r.returncode != 0:
        if "CMSIS-DAP" in output and ("mismatch" in output or "cannot" in output.lower()
                                       or "failed" in output.lower() or "error" in output.lower()):
            results.fatal("probe_not_busy",
                "CMSIS-DAP probe appears busy — is a VS Code debug session active? "
                "Close it before running tests.")
        else:
            results.fatal("probe_not_busy",
                f"OpenOCD probe test failed (exit {r.returncode}): "
                + output.strip().splitlines()[-1] if output.strip() else "unknown error")
    else:
        results.passed("probe_not_busy")


def check_python_venv(results, venv_python):
    if not os.path.isfile(venv_python):
        results.fatal("python_venv", f"venv not found: {venv_python}")
        return
    r = subprocess.run([venv_python, "-c", "import requests"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        results.fatal("python_venv",
            f"requests not importable in venv: {r.stderr.strip()}")
    else:
        results.passed("python_venv")


def check_args(results, args, suite_names):
    """
    Enforce --ssid/--password/--device-name only when the provisioning suite is
    being run.  When running other suites, record PASS so the runner isn't
    blocked on credentials that aren't needed yet.
    """
    needs_prov = any("test_provisioning" in s for s in suite_names)

    if args.ssid:
        detail = f"ssid={args.ssid}" + (f", password set" if args.password else ", no password (open network)")
        results.passed("args_wifi", detail)
    elif needs_prov:
        results.fatal("args_wifi", "--ssid required for provisioning")
    else:
        results.passed("args_wifi", "not provided (provisioning not selected)")

    if args.device_name:
        results.passed("args_device_name", f"device_name={args.device_name}")
    elif needs_prov:
        results.fatal("args_device_name", "--device-name required for provisioning")
    else:
        results.passed("args_device_name", "not provided (provisioning not selected)")


# ── Entry point ────────────────────────────────────────────────────────────

def run_all(results, args, project_root, suite_names):
    """Run all pre-flight checks in order.  Fatal failures raise RunnerFatal."""
    check_server_kill(results)
    check_ap_proxy(results)
    check_cmsis_dap(results)
    check_tool(results, "openocd_present", "openocd", "--version")
    check_tool(results, "picotool_present", "picotool", "version")
    check_build_artifact(results, "wp_uf2",
        os.path.join(project_root, "build", "WP", "WP.uf2"), "WP.uf2")
    check_build_artifact(results, "ep_uf2",
        os.path.join(project_root, "build", "EP", "EP.uf2"), "EP.uf2")
    check_build_artifact(results, "wpusbboot_bin",
        os.path.join(project_root, "build", "WpUsbBoot", "WpUsbBoot"), "WpUsbBoot")
    check_python_venv(results,
        os.path.join(project_root, "build", ".venv", "bin", "python3"))
    check_args(results, args, suite_names)
