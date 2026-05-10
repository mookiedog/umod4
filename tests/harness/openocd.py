"""
OpenOCD subprocess manager for the umod4 test harness.

Launches OpenOCD, waits for RTT to be found, starts RTT TCP servers,
then provides a context manager so tests can use RTT channels via TCP.

Usage:
    with OpenOCD() as ocd:
        ocd.wait_ready()   # blocks until RTT control block found
        # now connect sockets to ocd.rtt_port(channel)
"""

import subprocess
import threading
import socket
import time
import re
import os
import sys

OPENOCD     = "/usr/local/bin/openocd"
IFACE_CFG   = "interface/cmsis-dap.cfg"
TARGET_CFG  = "target/rp2350.cfg"
RTT_SEARCH  = ("0x20000000", "0x80000")   # base, size — full WP RAM

# TCP base port for RTT channels: channel N → RTT_PORT_BASE + N
RTT_PORT_BASE = 9000

# How long to wait for RTT control block after WP reset (seconds).
# WP boot takes ~2-3 s; 20 s gives plenty of margin.
RTT_TIMEOUT = 20.0


class OpenOCDError(Exception):
    pass


class OpenOCD:
    def __init__(self, adapter_speed=20000, verbose=False):
        self._adapter_speed = adapter_speed
        self._verbose       = verbose
        self._proc          = None
        self._log_lines     = []
        self._rtt_ready     = threading.Event()
        self._lock          = threading.Lock()

    # ------------------------------------------------------------------
    def start(self, reset=True):
        with self._lock:
            self._rtt_ready.clear()
            self._log_lines.clear()
        cmd = [
            OPENOCD,
            "-f", IFACE_CFG,
            "-f", TARGET_CFG,
            "-c", f"adapter speed {self._adapter_speed}",
            "-c", "init",
        ]
        if reset:
            cmd += ["-c", "reset run"]   # clean WP boot; omit after OTA reboot
        cmd += [
            "-c", f"rtt setup {RTT_SEARCH[0]} {RTT_SEARCH[1]} \"SEGGER RTT\"",
            "-c", "rtt start",
        ]
        # One RTT server per channel we care about (0-4)
        for ch in range(5):
            port = RTT_PORT_BASE + ch
            cmd += ["-c", f"rtt server start {port} {ch}"]

        self._proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        # Drain output on a background thread so the pipe never blocks
        t = threading.Thread(target=self._reader, daemon=True)
        t.start()

    def stop(self):
        if self._proc and self._proc.poll() is None:
            self._proc.terminate()
            try:
                self._proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self._proc.kill()
        self._proc = None

    @property
    def is_running(self):
        return self._proc is not None and self._proc.poll() is None

    def reconnect(self, timeout=RTT_TIMEOUT):
        """Stop current OpenOCD session and reconnect without resetting the target.

        Used after a target-initiated reboot (e.g. WP OTA) where the new firmware
        is already running and must not be disturbed by a debug reset.
        """
        self.stop()
        with self._lock:
            self._rtt_ready.clear()
            self._log_lines.clear()
        self.start(reset=False)
        self.wait_ready(timeout=timeout)

    def wait_ready(self, timeout=RTT_TIMEOUT):
        """Block until RTT control block is found, or raise OpenOCDError."""
        if not self._rtt_ready.wait(timeout=timeout):
            raise OpenOCDError(
                f"RTT control block not found within {timeout}s.\n"
                + self._last_log(20)
            )

    def rtt_port(self, channel):
        return RTT_PORT_BASE + channel

    # ------------------------------------------------------------------
    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *_):
        self.stop()

    # ------------------------------------------------------------------
    def _reader(self):
        for line in self._proc.stdout:
            line = line.rstrip()
            with self._lock:
                self._log_lines.append(line)
            if self._verbose:
                print(f"[openocd] {line}", flush=True)
            # RTT control block found when OpenOCD prints this:
            if "Control block found" in line:
                self._rtt_ready.set()

    def _last_log(self, n=20):
        with self._lock:
            return "\n".join(self._log_lines[-n:])
