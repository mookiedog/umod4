#!/usr/bin/env python3
"""
umod4 automated test runner.

Usage:
    build/.venv/bin/python3 tests/runner.py [suite ...] [--ssid S --password P --device-name N]

    suite          One or more suite names (e.g. test_basic test_wifi).  Omit to run all.
    --sta-ssid     WiFi (STA) SSID  (or set UMOD4_STA_SSID env var)
    --sta-password WiFi (STA) password  (or set UMOD4_STA_PASSWORD env var)
    --device-name  Device name to assign  (or set UMOD4_DEVICE_NAME env var)
    --ap-ssid      AP SSID to write to device config  (or set UMOD4_AP_SSID env var)
    --ap-password  AP password to write to device config  (or set UMOD4_AP_PASSWORD env var)
    --no-flash     Skip reflashing WP; run tests against already-loaded firmware

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
import contextlib
import io
import sys
import os
import importlib
import subprocess
import time

# Allow 'from harness.xxx import ...' and 'from suites.xxx import ...'
sys.path.insert(0, os.path.dirname(__file__))

from harness.openocd import OpenOCD, OpenOCDError, RTT_PORT_BASE
from harness import preflight
from harness import build_state
from harness.rtt import RttChannel, RttCapture, RttError
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
        self._entries      = []   # (status, suite, name, detail)
        self._suite_output = {}   # suite_name -> captured stdout text
        self._suite_ch0    = {}   # suite_name -> WP printf (ch0) text
        self._suite_heap   = {}   # suite_name -> (remaining, free)
        self._suite        = "preflight"
        self._t0           = time.monotonic()

    def start_suite(self, name):
        self._suite = name

    def start(self, name):
        print(f"  ----  {name}", end="\r", file=sys.__stdout__, flush=True)

    def passed(self, name, detail=""):
        self._record("PASS", name, detail)
        print(f"  PASS  {name}" + (f"  {detail}" if detail else ""), file=sys.__stdout__, flush=True)

    def failed(self, name, detail=""):
        self._record("FAIL", name, detail)
        print(f"  FAIL  {name}" + (f"  {detail}" if detail else ""), file=sys.__stdout__, flush=True)

    def warn(self, name, detail=""):
        """Record WARN — noted in report but does not fail the run."""
        self._record("WARN", name, detail)
        print(f"  WARN  {name}" + (f"  {detail}" if detail else ""), file=sys.__stdout__, flush=True)

    def abort(self, name, detail=""):
        """Record ABORT and stop the current suite."""
        self._record("ABORT", name, detail)
        print(f"  ABORT {name}" + (f"  {detail}" if detail else ""), file=sys.__stdout__, flush=True)
        raise SuiteAbort(name)

    def fatal(self, name, detail=""):
        """Record FATAL and stop the entire runner."""
        self._record("FATAL", name, detail)
        print(f"  FATAL {name}" + (f"  {detail}" if detail else ""), file=sys.__stdout__, flush=True)
        raise RunnerFatal(name)

    def record_suite_output(self, suite_name, text):
        self._suite_output[suite_name] = text

    def record_ch0_output(self, suite_name, text):
        self._suite_ch0[suite_name] = text

    def record_heap(self, suite_name, remaining, free):
        self._suite_heap[suite_name] = (remaining, free)

    def _record(self, status, name, detail):
        self._entries.append((status, self._suite, name, detail))

    @property
    def all_passed(self):
        return all(e[0] in ("PASS", "WARN") for e in self._entries)

    @property
    def counts(self):
        passed  = sum(1 for e in self._entries if e[0] == "PASS")
        warned  = sum(1 for e in self._entries if e[0] == "WARN")
        aborted = sum(1 for e in self._entries if e[0] in ("ABORT", "FATAL"))
        return passed, len(self._entries), aborted, warned

    def write_report(self, path):
        import datetime
        import shutil
        os.makedirs(os.path.dirname(path), exist_ok=True)
        passed, total, aborted, warned = self.counts
        duration = time.monotonic() - self._t0
        run_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        overall  = "PASS" if self.all_passed else "FAIL"
        summary  = f"{passed}/{total} passed"
        if warned:
            summary += f"  •  {warned} stale (--allow-stale)"
        if aborted:
            summary += f"  •  {aborted} aborted"

        lines = [
            "# umod4 Test Report", "",
            f"**Run:** {run_time}",
            f"**Duration:** {duration:.1f} s",
            f"**Status:** {overall} — {summary}",
            "", "---", "",
        ]

        # Group entries by suite, preserving run order
        suites_seen  = []
        suite_entries = {}
        for status, suite, name, detail in self._entries:
            if suite not in suite_entries:
                suite_entries[suite] = []
                suites_seen.append(suite)
            suite_entries[suite].append((status, name, detail))

        for suite_name in suites_seen:
            entries     = suite_entries[suite_name]
            suite_pass  = sum(1 for s, _, _ in entries if s == "PASS")
            lines.append(f"## {suite_name}  ({suite_pass}/{len(entries)})")
            lines.append("")
            lines.append("| Status | Test | Message |")
            lines.append("| --- | --- | --- |")
            for status, name, detail in entries:
                lines.append(f"| {status} | {name} | {str(detail).replace('|', chr(92) + '|')} |")
            heap = self._suite_heap.get(suite_name)
            if heap:
                remaining, free = heap
                lines.append("")
                lines.append(f"*Heap after: remaining={remaining} free={free}*")
            output = self._suite_output.get(suite_name, "").strip()
            if output:
                lines.append("")
                lines.append("```")
                lines.extend(output.splitlines())
                lines.append("```")
            ch0 = self._suite_ch0.get(suite_name, "").strip()
            if ch0:
                lines.append("")
                lines.append("**WP printf (ch0):**")
                lines.append("```")
                lines.extend(ch0.splitlines())
                lines.append("```")
            lines.append("")

        lines += ["---", "", summary]

        with open(path, "w") as f:
            f.write("\n".join(lines) + "\n")

        latest = os.path.join(os.path.dirname(path), "latest.md")
        shutil.copy2(path, latest)


# -------------------------------------------------------------------------
# Suite ordering is load-bearing — do not rearrange without understanding the
# dependencies below.
#
#  test_provisioning  Erases and reflashes WP; must run first so every
#                     subsequent suite runs against known-good WP firmware.
#  test_basic         WP/SD health checks; no external dependencies.
#  test_ep_swd        Verifies EP SWD connectivity; must precede test_ota_ep
#                     (the OTA path uses SWD to program EP).
#  test_wifi          Connects to WiFi and stores wp_ip in context; must
#                     precede test_ota_ep (upload uses the HTTP server).
#  test_ota_ep        Reflashes EP via HTTP+SWD.  EP carries the ECU firmware
#                     image, so the ECU is only on latest firmware AFTER this
#                     suite completes.  test_ecu must not run before this.
#  test_ecu           ECU sensor and timing checks.  Depends on EP (and
#                     therefore ECU firmware) being up-to-date.
#  test_ota_wp        Reflashes WP last: WP reboots at the end, which tears
#                     down the RTT session and ends the run.
SUITES = [
    "suites.test_provisioning",
    "suites.test_config_migration",
    "suites.test_basic",
    "suites.test_ep_swd",
    "suites.test_wifi",
    "suites.test_ota_ep",
    "suites.test_ecu",
    "suites.test_ota_wp",
]

def load_suites(names=None):
    if names:
        return [importlib.import_module(f"suites.{n.removeprefix('suites.')}") for n in names]
    return [importlib.import_module(s) for s in SUITES]


WP_VFY_CHANNEL = 1

def _check_heap(ocd):
    """Query WP heap via RTT. Returns (remaining, free) or (None, None) on failure."""
    import json
    try:
        with RttChannel(ocd.rtt_port(WP_VFY_CHANNEL)) as vfy:
            reply = vfy.command("heap", timeout=5.0)
            h = json.loads(reply).get("heap", {})
            return h.get("remaining"), h.get("free")
    except Exception:
        return None, None


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
    result = subprocess.run([WP_ENTER_BOOTSEL, WP_USB_BOOT_BINARY],
                            capture_output=True, text=True)
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise RuntimeError(f"wp_enter_bootsel failed (exit {result.returncode}): {detail}")

    print(f"Flashing WP from {WP_BUILD_DIR} ...", flush=True)
    result = subprocess.run([FLASH_WP, WP_BUILD_DIR],
                            capture_output=True, text=True)
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise RuntimeError(f"flash_WP failed (exit {result.returncode}): {detail}")
    print("Flash complete.\n", flush=True)


# -------------------------------------------------------------------------
def _parse_args():
    p = argparse.ArgumentParser(description="umod4 automated test runner",
                                add_help=True)
    p.add_argument("suites", nargs="*",
                   help="Suite names to run (default: all)")
    p.add_argument("--sta-ssid",     dest="ssid",
                   default=os.environ.get("UMOD4_STA_SSID", ""),
                   help="WiFi (STA) SSID for provisioning (or set UMOD4_STA_SSID)")
    p.add_argument("--sta-password", dest="password",
                   default=os.environ.get("UMOD4_STA_PASSWORD", ""),
                   help="WiFi (STA) password for provisioning (or set UMOD4_STA_PASSWORD)")
    p.add_argument("--device-name",  dest="device_name",
                   default=os.environ.get("UMOD4_DEVICE_NAME", "umod4_test_hw"),
                   help="Device name to assign during provisioning (or set UMOD4_DEVICE_NAME)")
    p.add_argument("--ap-ssid",      dest="ap_ssid",
                   default=os.environ.get("UMOD4_AP_SSID", ""),
                   help="AP SSID to write to device config (or set UMOD4_AP_SSID)")
    p.add_argument("--ap-password",  dest="ap_password",
                   default=os.environ.get("UMOD4_AP_PASSWORD", ""),
                   help="AP password to write to device config (or set UMOD4_AP_PASSWORD)")
    p.add_argument("--l000c", dest="l000c",
                   default=None, type=lambda x: int(x, 0),
                   help="Expected ECU L000C register value (hex or decimal; default: use test constant 0x10)")
    p.add_argument("--preflight-only", action="store_true", dest="preflight_only",
                   help="Run pre-flight checks only, then exit")
    p.add_argument("--no-flash", action="store_true", dest="no_flash",
                   help="Skip reflashing WP and run tests against the already-loaded firmware")
    p.add_argument("--allow-stale", action="store_true", dest="allow_stale",
                   help="Warn instead of abort when build artifacts are older than their sources")
    return p.parse_args()


def main():
    import datetime
    args    = _parse_args()
    suites  = load_suites(args.suites if args.suites else None)

    run_ts      = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    report_dir  = os.path.join(PROJECT_ROOT, "build", "test_reports")
    report_path = os.path.join(report_dir, f"run_{run_ts}.md")

    results = Results()
    ocd     = OpenOCD(verbose=False)
    ch0     = RttCapture(RTT_PORT_BASE + 0)   # WP printf; reconnects automatically

    try:
        print("[harness.preflight]", file=sys.__stdout__, flush=True)
        results.start_suite("preflight")

        suite_names = [s.__name__ for s in suites]
        preflight.run_all(results, args, PROJECT_ROOT, suite_names)

        try:
            ensure_attached(CMSIS_DAP_HW_ID)
        except RuntimeError as e:
            results.fatal("cmsis_dap_attach", str(e))

        print("[harness.build_state]", file=sys.__stdout__, flush=True)
        results.start_suite("build_state")
        build_state.run_all(results, PROJECT_ROOT, allow_stale=args.allow_stale)

        if not args.preflight_only:
            # Suites that manage their own WP flash and OpenOCD lifecycle.
            # When ALL selected suites are self-managed, the runner skips the
            # normal pre-suite flash + OCD startup; the first self-managed suite
            # is responsible for starting OpenOCD before subsequent suites run.
            # When mixed with normal suites (e.g. test_basic + test_config_migration),
            # the runner starts OCD normally; the self-managed suite restarts it
            # in its own first step.
            _SELF_MANAGED = {"test_provisioning", "test_config_migration"}
            prov_selected = all(
                any(name in s.__name__ for name in _SELF_MANAGED)
                for s in suites
            )

            if not prov_selected:
                # Normal run: (optionally) flash WP then start OpenOCD.
                # Self-managed suites skip this and handle erase+flash+OCD themselves.
                if args.no_flash:
                    print("Skipping WP flash (--no-flash).", flush=True)
                else:
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
                "ap_ssid":     args.ap_ssid,
                "ap_password": args.ap_password,
                "l000c":       args.l000c,
            }
            for suite in suites:
                suite_name = suite.__name__.removeprefix("suites.")
                results.start_suite(suite_name)
                ch0.mark()
                print(f"[{suite.__name__}]", file=sys.__stdout__, flush=True)
                buf = io.StringIO()
                try:
                    with contextlib.redirect_stdout(buf):
                        suite.run(ocd, results, context)
                except SuiteAbort:
                    pass   # suite recorded ABORT; continue to next suite
                captured = buf.getvalue().strip()
                if captured:
                    results.record_suite_output(suite_name, captured)
                ch0_text = ch0.since_mark().strip()
                if ch0_text:
                    results.record_ch0_output(suite_name, ch0_text)
                remaining, free = _check_heap(ocd)
                if remaining is not None:
                    results.record_heap(suite_name, remaining, free)
                    print(f"  heap: remaining={remaining} free={free}",
                          file=sys.__stdout__, flush=True)
                print(file=sys.__stdout__)

    except RunnerFatal:
        pass   # FATAL recorded; fall through to report + exit 1

    finally:
        ch0.stop()
        ocd.stop()
        ensure_detached(CMSIS_DAP_HW_ID)

    results.write_report(report_path)
    passed, total, aborted, warned = results.counts
    summary = f"{passed}/{total} passed"
    if warned:
        summary += f"  •  {warned} stale (--allow-stale)"
    if aborted:
        summary += f"  •  {aborted} aborted"
    print(f"{'OK' if results.all_passed else 'FAILED'}  {summary}")
    print(f"Report: {report_path}")
    sys.exit(0 if results.all_passed else 1)


if __name__ == "__main__":
    main()
