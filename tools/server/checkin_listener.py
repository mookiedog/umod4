"""UDP listener for device check-in notifications.

This module listens for UDP check-in packets from umod4 devices when they
connect to WiFi. When a check-in is received, it triggers the auto-download
process.
"""

import errno
import socket
import json
import threading
from typing import Callable, Optional

CHECKIN_PORT = 54549  # UDP port devices broadcast/unicast check-ins to


class CheckInListener:
    """UDP listener for device check-in notifications."""

    def __init__(self, port: int = CHECKIN_PORT, host: str = '0.0.0.0'):
        """Initialize check-in listener.

        Args:
            port: UDP port to listen on (default: CHECKIN_PORT)
            host: Host address to bind to (default: 0.0.0.0 for all interfaces)
        """
        self.port = port
        self.host = host
        self.socket = None
        self.thread = None
        self.running = False
        self.error = None          # Why start() failed, as text (None if it did not fail)
        self.port_in_use = False   # True if start() failed because another program has the port
        self.on_device_checkin = None  # Callback: on_device_checkin(device_mac, device_ip)

    def start(self):
        """Start the UDP listener in a background thread.

        Returns True if the listener is running.  Returns False if it could not start; then
        self.error says why, and self.port_in_use is True if the cause is that another program
        (most likely a second umod4 server) already has the port.
        """
        if self.running:
            return True

        self.error = None
        self.port_in_use = False
        try:
            # Create UDP socket.  Do NOT set SO_REUSEADDR: for UDP it lets a second server bind
            # the same port without any error, and two servers would then compete for the same
            # check-ins.  UDP has no TIME_WAIT, so a restart never needs it.  On Windows, ask for
            # exclusive use of the port as well.
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            if hasattr(socket, 'SO_EXCLUSIVEADDRUSE'):
                self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            self.socket.bind((self.host, self.port))
            self.socket.settimeout(1.0)  # 1 second timeout for clean shutdown

            self.running = True

            # Start listener thread
            self.thread = threading.Thread(target=self._listen_loop, daemon=True)
            self.thread.start()

            print(f"CheckInListener: Listening for device check-ins on UDP port {self.port}")
            return True

        except Exception as e:
            self.running = False
            self.error = str(e)
            # 10048 is WSAEADDRINUSE, the Windows spelling of "address already in use"
            self.port_in_use = (getattr(e, 'errno', None) == errno.EADDRINUSE
                                or getattr(e, 'winerror', None) == 10048)
            if self.socket is not None:
                self.socket.close()
                self.socket = None
            if self.port_in_use:
                print(f"CheckInListener: UDP port {self.port} is already in use. "
                      f"Another umod4 server is probably running: close it and start this one again.")
            else:
                print(f"CheckInListener: Failed to start: {e}")
            return False

    def stop(self):
        """Stop the UDP listener."""
        if not self.running:
            return

        self.running = False

        if self.thread:
            self.thread.join(timeout=2)

        if self.socket:
            self.socket.close()
            self.socket = None

        print("CheckInListener: Stopped")

    def _listen_loop(self):
        """Main listening loop (runs in background thread)."""
        while self.running:
            try:
                # Receive UDP packet (max 1024 bytes)
                data, addr = self.socket.recvfrom(1024)

                # Parse JSON payload
                try:
                    payload = json.loads(data.decode('utf-8'))
                    msg_type = payload.get('type', 'checkin')
                    device_mac = payload.get('device_mac')
                    device_ip = payload.get('ip')

                    if msg_type == 'discover':
                        # Device is broadcasting to find the server — reply so it learns our IP:port
                        reply = json.dumps({"type": "announce", "port": self.port}).encode('utf-8')
                        self.socket.sendto(reply, addr)
                        device_name = payload.get('device_name', device_mac)
                        print(f"CheckInListener: Discovery from {device_name} ({device_mac}) at {device_ip} — replied")
                    else:
                        if device_mac and device_ip:
                            print(f"CheckInListener: Device {device_mac} checked in from {device_ip}")
                        else:
                            print(f"CheckInListener: Invalid check-in payload: {payload}")

                    # Fire callback for both discover and regular check-ins
                    if device_mac and device_ip and self.on_device_checkin:
                        try:
                            self.on_device_checkin(device_mac, device_ip)
                        except Exception as e:
                            print(f"CheckInListener: Error in callback: {e}")

                except json.JSONDecodeError as e:
                    print(f"CheckInListener: Invalid JSON from {addr}: {data[:100]}")

            except socket.timeout:
                # Timeout is normal, allows checking self.running flag
                continue

            except Exception as e:
                if self.running:  # Only log errors if we're supposed to be running
                    print(f"CheckInListener: Error receiving packet: {e}")

    def set_callback(self, callback: Callable[[str, str], None]):
        """Set callback function for device check-in events.

        Args:
            callback: Function with signature callback(device_mac: str, device_ip: str)
        """
        self.on_device_checkin = callback
