"""
RTT channel I/O over OpenOCD's TCP RTT server.

RttChannel wraps a TCP socket to one OpenOCD RTT server port.
- send(text)          writes to the channel's DOWN buffer (host→device)
- readline(timeout)   reads one newline-terminated line from the UP buffer
- command(cmd, timeout) sends a command and returns the matching VFY: reply
"""

import socket
import time


class RttError(Exception):
    pass


class RttChannel:
    def __init__(self, port, connect_timeout=5.0):
        self._port    = port
        self._buf     = b""
        self._sock    = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.settimeout(connect_timeout)
        self._sock.connect(("localhost", port))
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
                if chunk:
                    self._buf += chunk
            except socket.timeout:
                pass
            finally:
                self._sock.settimeout(None)

    def command(self, cmd, timeout=5.0):
        """
        Send a VFY command and return the matching 'VFY: <cmd> ...' reply line.
        Raises RttError if no matching reply arrives within timeout.
        """
        self.send(cmd)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            line = self.readline(timeout=remaining)
            if line.startswith(f"VFY: {cmd.strip()}"):
                return line
        raise RttError(f"No VFY reply for '{cmd}' within {timeout}s")
