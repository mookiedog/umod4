#!/usr/bin/env python3
"""
umod4 automated test runner.

Usage:
    build/.venv/bin/python3 tests/runner.py [suite ...] [--ssid S --password P --device-name N]

    suite       One or more suite names (e.g. test_basic test_wifi).  Omit to run all.
    --ssid      WiFi SSID for provisioning (or set UMOD4_WIFI_SSID env var)
    --password  WiFi password for provisioning (or set UMOD4_WIFI_PASSWORD env var)
    --device-name  Device name to assign during provisioning

The build venv is required because test_ota_ep imports DeviceClient which needs 'requests'.
All other suites use only stdlib.

The runner:
  1. Runs pre-flight checks (USB devices, tools, build artifacts, args)
  2. Erases and flashes WP with the latest firmware (always destructive)
  3. Starts OpenOCD and resets WP (guarantees clean boot)
  4. Waits for RTT to come up
  5. Runs each test suite in order, passing a shared context dict between them
  6. Writes a Markdown report to build/test_reports/ and exits 0 (all pass) or 1 (any fail)
"""

import argparse
import sys
import os
import importlib
import subprocess
import time

# Allow 'from harness.xxx import ...' and 'from suites.xxx import ...'
sys.path.insert(0, os.path.dirname(__file__))

from harness.openocd import OpenOCD, OpenOCDError
from harness import preflight
from harness.usbipd import ensure_attached, ensure_detached

CMSIS_DAP_HW_ID = "2e8a:000c"

PROJECT_ROOT       = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WP_ENTER_BOOTSEL   = os.path.join(PROJECT_ROOT, "tools", "wp_enter_bootsel")
WP_USB_BOOT_BINARY = os.path.join(PROJECT_ROOT, "build", "WpUsbBoot", "WpUsbBoot")
FLASH_WP           = os.path.join(PROJECT_ROOT, "tools", "flash_WP")
WP_BUILD_DIR       = os.path.join(PROJECT_ROOT, "build", "WP")


# -------------------------------------------------------------------------
class SuiteAbort(Exception):
    """Raised by results.abort() — stops the current suite; runner continues."""
    pass


class RunnerFatal(Exception):
    """Raised by results.fatal() — stops the entire runner."""
    pass


class Results:
    def __init__(self):
        self._entries = []   # (status, suite, name, detail)
        self._suite   = "preflight"
        self._t0      = time.monotonic()

    def start_suite(self, name):
        self._suite = name

    def start(self, name):
        pass   # kept for API compatibility

    def passed(self, name, detail=""):
        self._record("PASS", name, detail)
        print(f"  PASS  {name}" + (f"  {detail}" if detail else ""))

    def failed(self, name, detail=""):
        self._record("FAIL", name, detail)
        print(f"  FAIL  {name}" + (f"  {detail}" if detail else ""))

    def abort(self, name, detail=""):
        """Record ABORT and stop the current suite."""
        self._record("ABORT", name, detail)
        print(f"  ABORT {name}" + (f"  {detail}" if detail else ""))
        raise SuiteAbort(name)

    def fatal(self, name, detail=""):
        """Record FATAL and stop the entire runner."""
        self._record("FATAL", name, detail)
        print(f"  FATAL {name}" + (f"  {detail}" if detail else ""))
        raise RunnerFatal(name)

    def _record(self, status, name, detail):
        self._entries.append((status, self._suite, name, detail))

    @property
    def all_passed(self):
        return all(e[0] == "PASS" for e in self._entries)

    @property
    def counts(self):
        passed  = sum(1 for e in self._entries if e[0] == "PASS")
        aborted = sum(1 for e in self._entries if e[0] in ("ABORT", "FATAL"))
        return passed, len(self._entries), aborted

    def write_report(self, path):
        import datetime
        import shutil
        os.makedirs(os.path.dirname(path), exist_ok=True)
        passed, total, aborted = self.counts
        duration = time.monotonic() - self._t0
        run_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        overall  = "PASS" if self.all_passed else "FAIL"
        summary  = f"{passed}/{total} passed"
        if aborted:
            summary += f"  •  {aborted} aborted"

        lines = [
            "# umod4 Test Report", "",
            f"**Run:** {run_time}",
            f"**Duration:** {duration:.1f} s",
            f"**Status:** {overall} — {summary}",
            "", "---", "",
            "| Status | Suite | Test | Message |",
            "| --- | --- | --- | --- |",
        ]
        for status, suite, name, detail in self._entries:
            lines.append(f"| {status} | {suite} | {name} | {detail.replace('|', chr(92) + '|')} |")
        lines += ["", "---", "", summary]

        with open(path, "w") as f:
            f.write("\n".join(lines) + "\n")

        latest = os.path.join(os.path.dirname(path), "latest.md")
        shutil.copy2(path, latest)


# -------------------------------------------------------------------------
SUITES = [
    "suites.test_provisioning",
    "suites.test_basic",
    "suites.test_ep_swd",
    "suites.test_wifi",
    "suites.test_ota_ep",
    "suites.test_ota_wp",
]

def load_suites(names=None):
    if names:
        return [importlib.import_module(f"suites.{n.removeprefix('suites.')}") for n in names]
    return [importlib.import_module(s) for s in SUITES]


def flash_wp():
    """Put WP into BOOTSEL mode via SWD, then flash the latest firmware.
    Raises RuntimeError on any failure so the caller can record it properly."""
    for label, path in [
        ("wp_enter_bootsel", WP_ENTER_BOOTSEL),
        ("flash_WP",         FLASH_WP),
    ]:
        if not os.path.isfile(path):
            raise RuntimeError(f"{label} script not found at {path}")
    if not os.path.isfile(WP_USB_BOOT_BINARY):
        raise RuntimeError(f"WpUsbBoot binary not found at {WP_USB_BOOT_BINARY}")
    if not os.path.isdir(WP_BUILD_DIR):
        raise RuntimeError(f"WP build directory not found at {WP_BUILD_DIR}")

    print("Entering WP BOOTSEL mode via SWD...", flush=True)
    result = subprocess.run([WP_ENTER_BOOTSEL, WP_USB_BOOT_BINARY])
    if result.returncode != 0:
        raise RuntimeError(f"wp_enter_bootsel failed (exit {result.returncode})")

    print(f"Flashing WP from {WP_BUILD_DIR} ...", flush=True)
    result = subprocess.run([FLASH_WP, WP_BUILD_DIR])
    if result.returncode != 0:
        raise RuntimeError(f"flash_WP failed (exit {result.returncode})")
    print("Flash complete.\n", flush=True)


# -------------------------------------------------------------------------
def _parse_args():
    p = argparse.ArgumentParser(description="umod4 automated test runner",
                                add_help=True)
    p.add_argument("suites", nargs="*",
                   help="Suite names to run (default: all)")
    p.add_argument("--ssid",        default=os.environ.get("UMOD4_WIFI_SSID", ""),
                   help="WiFi SSID for provisioning (or set UMOD4_WIFI_SSID)")
    p.add_argument("--password",    default=os.environ.get("UMOD4_WIFI_PASSWORD", ""),
                   help="WiFi password for provisioning (or set UMOD4_WIFI_PASSWORD)")
    p.add_argument("--device-name", default="umod4_test_hw", dest="device_name",
                   help="Device name to assign during provisioning (default: umod4_test_hw)")
    p.add_argument("--preflight-only", action="store_true", dest="preflight_only",
                   help="Run pre-flight checks only, then exit")
    # Future: --restore-board for non-destructive snapshot/restore mode
    return p.parse_args()


def main():
    import datetime
    args    = _parse_args()
    suites  = load_suites(args.suites if args.suites else None)

    run_ts      = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    report_dir  = os.path.join(PROJECT_ROOT, "build", "test_reports")
    report_path = os.path.join(report_dir, f"run_{run_ts}.md")

    print("umod4 test harness")
    print("------------------")

    results = Results()
    ocd     = OpenOCD(verbose=False)

    try:
        # ── Pre-flight (flat list — every item uses results.fatal() on failure) ──
        results.start_suite("preflight")

        suite_names = [s.__name__ for s in suites]
        preflight.run_all(results, args, PROJECT_ROOT, suite_names)

        try:
            ensure_attached(CMSIS_DAP_HW_ID)
        except RuntimeError as e:
            results.fatal("cmsis_dap_attach", str(e))

        if not args.preflight_only:
            prov_selected = any("test_provisioning" in s.__name__ for s in suites)

            if not prov_selected:
                # Normal run: flash WP then start OpenOCD.
                # Provisioning suite skips this and handles erase+flash+OCD itself.
                try:
                    flash_wp()
                except RuntimeError as e:
                    results.fatal("wp_flash", str(e))

                print("Starting OpenOCD...", flush=True)
                try:
                    ocd.start(reset=True)
                    ocd.wait_ready()
                except (OpenOCDError, OSError) as e:
                    results.fatal("openocd_rtt", str(e))

                # Guard against a pending TBYB warm-boot: if the last flash used
                # 'picotool load -p 1', the bootrom boots in TBYB mode, check_tbyb()
                # commits (2 s busy-wait) then watchdog-resets WP.  OpenOCD fires
                # "RTT ready" during the first (warm) boot, but VfyTask never runs
                # until the subsequent cold boot.  Sleeping here lets that entire
                # cycle complete before the first test command is sent.
                time.sleep(5.0)
                print("RTT ready.\n")

            # ── Test suites ───────────────────────────────────────────────────
            context = {    # shared state across suites
                "ssid":        args.ssid,
                "password":    args.password,
                "device_name": args.device_name,
            }
            for suite in suites:
                results.start_suite(suite.__name__.removeprefix("suites."))
                print(f"[{suite.__name__}]")
                try:
                    suite.run(ocd, results, context)
                except SuiteAbort:
                    pass   # suite recorded ABORT; continue to next suite
                print()

    except RunnerFatal:
        pass   # FATAL recorded; fall through to report + exit 1

    finally:
        ocd.stop()
        ensure_detached(CMSIS_DAP_HW_ID)

    results.write_report(report_path)
    passed, total, aborted = results.counts
    summary = f"{passed}/{total} passed"
    if aborted:
        summary += f"  •  {aborted} aborted"
    print(f"{'OK' if results.all_passed else 'FAILED'}  {summary}")
    print(f"Report: {report_path}")
    sys.exit(0 if results.all_passed else 1)


if __name__ == "__main__":
    main()
