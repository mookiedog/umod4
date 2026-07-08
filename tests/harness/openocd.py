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
        """Block until RTT control block is found AND the VFY channel
        responds, confirming the firmware is fully booted.
        The RTT control block is a static variable found early in boot,
        but VfyTask may not be running yet — without this check, commands
        sent to the VFY channel go into the void."""
        if not self._rtt_ready.wait(timeout=timeout):
            raise OpenOCDError(
                f"RTT control block not found within {timeout}s.\n"
                + self._last_log(20)
            )
        self._wait_vfy_channel(timeout=timeout)

    def rtt_port(self, channel):
        return RTT_PORT_BASE + channel

    def tcl_command(self, cmd, timeout=30.0):
        """Send a command to the running OpenOCD's TCL port (6666).
        Returns the response string. Raises OpenOCDError on failure."""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(timeout)
            sock.connect(("localhost", 6666))
            # OpenOCD TCL protocol: send command + 0x1a terminator
            sock.sendall((cmd + "\x1a").encode())
            # Read response until 0x1a terminator
            response = b""
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response += chunk
                if b"\x1a" in chunk:
                    break
            sock.close()
            return response.rstrip(b"\x1a").decode().strip()
        except Exception as e:
            raise OpenOCDError(f"TCL command failed: {e}")

    def program_flash(self, filepath, address):
        """Program a binary file to flash via the running OpenOCD.
        Resets and halts the target (clean state), programs, but does NOT reset."""
        self.tcl_command("reset halt")
        result = self.tcl_command(f"program {{{filepath}}} 0x{address:08X}")
        if "Error" in result:
            raise OpenOCDError(f"Flash programming failed: {result}")

    def reset_and_wait(self, timeout=RTT_TIMEOUT):
        """Issue a reset and wait for RTT to come back.
        Restarts RTT scanning and TCP servers so OpenOCD finds the new
        control block and clients can reconnect to the RTT channels."""
        with self._lock:
            self._rtt_ready.clear()
        self.tcl_command("reset run")
        self.tcl_command("rtt stop")
        self.tcl_command(f"rtt setup {RTT_SEARCH[0]} {RTT_SEARCH[1]} \"SEGGER RTT\"")
        self.tcl_command("rtt start")
        for ch in range(5):
            port = RTT_PORT_BASE + ch
            try:
                self.tcl_command(f"rtt server stop {port}")
            except OpenOCDError:
                pass
            self.tcl_command(f"rtt server start {port} {ch}")
        self.wait_ready(timeout=timeout)

    def _wait_vfy_channel(self, timeout=RTT_TIMEOUT):
        """Poll the VFY RTT channel until it responds, confirming VfyTask
        is running. The RTT control block can be found before the firmware
        has initialized channel buffers — this ensures the channel is live."""
        from harness.rtt import RttChannel, RttError
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                with RttChannel(self.rtt_port(1), connect_timeout=2.0) as vfy:
                    reply = vfy.command("heap", timeout=2.0)
                    if reply:
                        return
            except Exception:
                pass
            time.sleep(0.5)
        raise OpenOCDError(f"VFY channel not responding within {timeout}s")

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
