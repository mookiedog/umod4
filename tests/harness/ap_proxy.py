"""
ApProxy — USB serial HTTP proxy for a umod4 device in AP mode.

A Pico W running tools/ap_proxy firmware appears as a USB CDC-ACM serial
port. It connects to a umod4_XXXX WiFi AP and forwards HTTP requests sent
over the serial link, returning the responses.

Protocol: line-based text, newline-terminated.

  SCAN                       -> OK [ssid1 ssid2 ...]
  CONNECT <ssid> [password]  -> OK connected <ip>    (password defaults to ssid)
  STATUS                     -> OK connected <ssid> <ip>  |  OK disconnected
  GET <path>                 -> OK <http_status> <json_body>
  POST <path> <json>         -> OK <http_status> <json_body>
  DISCONNECT                 -> OK
  PING                       -> OK
"""

import json
import time

import serial
import serial.tools.list_ports

from harness.usb_ids import AP_PROXY_VID, AP_PROXY_PID


def find_port(vid=AP_PROXY_VID, pid=AP_PROXY_PID):
    """Return the serial device path for a USB device with vid:pid, or None."""
    for p in serial.tools.list_ports.comports():
        if p.vid == vid and p.pid == pid:
            return p.device
    return None


class ApProxyError(Exception):
    pass

class ApProxyAuthError(ApProxyError):
    """Raised when the AP rejects credentials (wrong password)."""
    pass


class ApProxy:
    AP_IP = "192.168.4.1"

    def __init__(self, port, baud=115200, cmd_timeout=15.0):
        self._ser = serial.Serial(port, baud, timeout=1.0)
        self._cmd_timeout = cmd_timeout
        time.sleep(0.5)          # let the port stabilise after open
        self._ser.reset_input_buffer()

    def close(self):
        if self._ser and self._ser.is_open:
            self._ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _send(self, line: str):
        self._ser.reset_input_buffer()
        self._ser.write((line.rstrip('\n') + '\n').encode())
        self._ser.flush()

    def _recv(self, timeout: float) -> str:
        """Read lines until one starts with OK or ERR, or timeout."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = self._ser.readline()
            if raw:
                line = raw.decode(errors='replace').strip()
                if line.startswith('OK') or line.startswith('ERR'):
                    return line
        raise ApProxyError(f"Timeout ({timeout}s) waiting for response")

    def _cmd(self, line: str, timeout: float | None = None) -> str:
        if timeout is None:
            timeout = self._cmd_timeout
        self._send(line)
        return self._recv(timeout)

    @staticmethod
    def _parse_http(resp: str) -> tuple[int, dict]:
        """Parse 'OK <status_code> <json_body>' into (code, dict)."""
        if resp.startswith('ERR'):
            raise ApProxyError(f"proxy error: {resp}")
        parts = resp.split(None, 2)
        if len(parts) < 2:
            raise ApProxyError(f"malformed response: {resp!r}")
        try:
            code = int(parts[1])
        except ValueError:
            raise ApProxyError(f"bad status code in: {resp!r}")
        body: dict = {}
        if len(parts) == 3:
            try:
                body = json.loads(parts[2])
            except json.JSONDecodeError:
                body = {'raw': parts[2]}
        return code, body

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def ping(self) -> bool:
        """Return True if the proxy Pico is responsive."""
        try:
            resp = self._cmd("PING", timeout=3.0)
            return resp.startswith('OK')
        except ApProxyError:
            return False

    def scan(self, timeout: float = 20.0) -> list[str]:
        """
        Scan for umod4_XXXX networks.
        Returns a (possibly empty) list of SSIDs.
        """
        resp = self._cmd("SCAN", timeout=timeout)
        if resp.startswith('ERR'):
            raise ApProxyError(f"SCAN failed: {resp}")
        # "OK ssid1 ssid2 ..." or just "OK"
        parts = resp.split()
        return parts[1:]

    def connect(self, ssid: str, password: str | None = None,
                timeout: float = 30.0) -> str:
        """
        Connect to ssid. Password defaults to the SSID (umod4 AP default).
        Returns the IP address assigned by DHCP (should be 192.168.4.x).
        """
        pw = password if password is not None else ssid
        resp = self._cmd(f"CONNECT {ssid} {pw}", timeout=timeout)
        if resp.startswith('ERR'):
            if resp.endswith('-3'):   # CYW43_LINK_BADAUTH
                raise ApProxyAuthError(f"CONNECT failed (bad password): {resp}")
            raise ApProxyError(f"CONNECT failed: {resp}")
        # "OK connected <ip>"
        parts = resp.split()
        return parts[2] if len(parts) >= 3 else self.AP_IP

    def status(self) -> dict:
        """
        Return connection state.
        Keys: 'connected' (bool), and if connected: 'ssid', 'ip'.
        """
        resp = self._cmd("STATUS", timeout=5.0)
        parts = resp.split()
        if resp.startswith('ERR') or len(parts) < 2:
            return {'connected': False}
        if parts[1] == 'connected' and len(parts) >= 4:
            return {'connected': True, 'ssid': parts[2], 'ip': parts[3]}
        return {'connected': False}

    def get(self, path: str, timeout: float = 10.0) -> tuple[int, dict]:
        """HTTP GET <path> on the AP. Returns (http_status, json_body)."""
        resp = self._cmd(f"GET {path}", timeout=timeout)
        return self._parse_http(resp)

    def post(self, path: str, body: dict,
             timeout: float = 10.0) -> tuple[int, dict]:
        """HTTP POST <path> with JSON body. Returns (http_status, json_body)."""
        body_str = json.dumps(body, separators=(',', ':'))
        resp = self._cmd(f"POST {path} {body_str}", timeout=timeout)
        return self._parse_http(resp)

    def disconnect(self):
        """Disconnect from the AP."""
        resp = self._cmd("DISCONNECT", timeout=5.0)
        if resp.startswith('ERR'):
            raise ApProxyError(f"DISCONNECT failed: {resp}")

    def find_and_connect(self, scan_retries: int = 3,
                         scan_timeout: float = 20.0,
                         connect_timeout: float = 30.0) -> str:
        """
        Scan for a umod4_XXXX network and connect to it automatically.
        Returns the assigned IP address.
        Raises ApProxyError if no umod4 network is found/connected after all retries.
        """
        last_error = None
        for attempt in range(scan_retries):
            ssids = self.scan(timeout=scan_timeout)
            umod4 = [s for s in ssids if s.startswith('umod4_')]
            if umod4:
                ssid = umod4[0]
                print(f"  proxy: found {ssid}, connecting...")
                try:
                    return self.connect(ssid, timeout=connect_timeout)
                except ApProxyAuthError:
                    raise  # wrong password — no point retrying
                except ApProxyError as e:
                    last_error = e
                    print(f"  proxy: connect failed ({e}), retrying in 5s...")
            else:
                last_error = ApProxyError(f"No umod4_XXXX network found (attempt {attempt + 1})")
                print(f"  proxy: no umod4 network found, retrying in 5s...")
            if attempt < scan_retries - 1:
                time.sleep(5.0)
        raise ApProxyError(
            f"Could not connect after {scan_retries} attempts: {last_error}")
