"""
RTT channel I/O over OpenOCD's TCP RTT server.

RttChannel wraps a TCP socket to one OpenOCD RTT server port.
- send(text)          writes to the channel's DOWN buffer (host→device)
- readline(timeout)   reads one newline-terminated line from the UP buffer
- command(cmd, timeout) sends a command and returns the matching JSON reply
"""

import socket
import threading
import time


class RttError(Exception):
    pass


class RttCapture:
    """
    Passively drains one RTT channel into a line buffer.
    Starts immediately and reconnects automatically when OpenOCD restarts.
    Used to capture WP printf output (ch0) across all test suites.

    Usage:
        cap = RttCapture(port)       # starts background thread
        cap.mark()                   # call before each suite
        text = cap.since_mark()      # call after each suite
        cap.stop()                   # call in finally block
    """

    def __init__(self, port):
        self._port   = port
        self._lines  = []
        self._lock   = threading.Lock()
        self._stop   = threading.Event()
        self._mark   = 0
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        self._stop.set()

    def mark(self):
        """Mark the start of a new capture window (call before each suite)."""
        with self._lock:
            self._mark = len(self._lines)

    def since_mark(self):
        """Return lines captured since the last mark() call, as a single string."""
        with self._lock:
            return "\n".join(self._lines[self._mark:])

    def _run(self):
        buf = b""
        while not self._stop.is_set():
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(1.0)
                try:
                    sock.connect(("localhost", self._port))
                except (ConnectionRefusedError, socket.timeout, OSError):
                    sock.close()
                    time.sleep(0.5)
                    continue
                buf = b""
                while not self._stop.is_set():
                    sock.settimeout(0.1)
                    try:
                        chunk = sock.recv(256)
                        if not chunk:
                            break
                        buf += chunk
                        while b"\n" in buf:
                            idx  = buf.index(b"\n")
                            line = buf[:idx].decode(errors="replace").rstrip("\r")
                            buf  = buf[idx + 1:]
                            with self._lock:
                                self._lines.append(line)
                    except socket.timeout:
                        pass
                    except OSError:
                        break
                sock.close()
            except Exception:
                time.sleep(0.5)


class RttChannel:
    def __init__(self, port, connect_timeout=5.0):
        self._port = port
        self._buf  = b""
        # Retry loop: OpenOCD may still be starting the RTT TCP server when
        # wait_ready() returns (race between "Control block found" log line
        # and the server socket actually listening).
        deadline = time.monotonic() + connect_timeout
        while True:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.settimeout(min(0.5, deadline - time.monotonic()))
            try:
                self._sock.connect(("localhost", port))
                break
            except (ConnectionRefusedError, socket.timeout):
                self._sock.close()
                if time.monotonic() >= deadline:
                    raise RttError(f"RTT port {port} not available after {connect_timeout}s")
                time.sleep(0.1)
        self._sock.settimeout(None)   # switch to blocking after connect

    def close(self):
        self._sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    # ------------------------------------------------------------------
    def send(self, text):
        """Send text to the channel's DOWN buffer (device receives it)."""
        if not text.endswith("\n"):
            text += "\n"
        self._sock.sendall(text.encode())

    def readline(self, timeout=5.0):
        """
        Read one newline-terminated line from the UP buffer.
        Returns the line without the trailing newline.
        Raises RttError on timeout.
        """
        deadline = time.monotonic() + timeout
        while True:
            if b"\n" in self._buf:
                idx = self._buf.index(b"\n")
                line = self._buf[:idx].decode(errors="replace").rstrip("\r")
                self._buf = self._buf[idx + 1:]
                return line

            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RttError(
                    f"Timeout waiting for line on port {self._port}. "
                    f"Buffer so far: {self._buf!r}"
                )

            self._sock.settimeout(min(remaining, 0.1))
            try:
                chunk = self._sock.recv(256)
                if not chunk:
                    raise OSError("RTT socket closed by peer")
                self._buf += chunk
            except socket.timeout:
                pass
            finally:
                self._sock.settimeout(None)

    def wait_for(self, prefix, timeout=30.0):
        """
        Read lines until one starts with prefix. Returns the matching line.
        Lines that don't match are silently discarded.
        Raises RttError if prefix not seen within timeout.
        """
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RttError(f"Timeout waiting for '{prefix}' after {timeout}s")
            try:
                line = self.readline(timeout=min(remaining, 1.0))
            except RttError:
                continue  # inner timeout — check outer deadline on next loop
            except OSError as e:
                raise RttError(f"Connection lost waiting for '{prefix}': {e}")
            if line.startswith(prefix):
                return line

    def command(self, cmd, timeout=5.0):
        """
        Send a VFY command and return the matching JSON reply line.
        Matches any line that starts with '{' and contains '"<cmd_name>":'.
        Raises RttError if no matching reply arrives within timeout.
        """
        self.send(cmd)
        cmd_name = cmd.strip().split()[0]   # match on command name only, ignoring arguments
        key_fragment = f'"{cmd_name}":'
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            line = self.readline(timeout=remaining)
            if line.startswith("{") and key_fragment in line:
                return line
        raise RttError(f"No JSON reply for '{cmd}' within {timeout}s")
