"""UDP listener for device check-in notifications.

This module listens for UDP check-in packets from umod4 devices when they
connect to WiFi. When a check-in is received, it triggers the auto-download
process.
"""

import socket
import json
import threading
from typing import Callable, Optional


class CheckInListener:
    """UDP listener for device check-in notifications."""

    def __init__(self, port: int = 8081, host: str = '0.0.0.0'):
        """Initialize check-in listener.

        Args:
            port: UDP port to listen on (default: 8081)
            host: Host address to bind to (default: 0.0.0.0 for all interfaces)
        """
        self.port = port
        self.host = host
        self.socket = None
        self.thread = None
        self.running = False
        self.on_device_checkin = None  # Callback: on_device_checkin(device_mac, device_ip)

    def start(self):
        """Start the UDP listener in a background thread."""
        if self.running:
            return

        try:
            # Create UDP socket
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.socket.bind((self.host, self.port))
            self.socket.settimeout(1.0)  # 1 second timeout for clean shutdown

            self.running = True

            # Start listener thread
            self.thread = threading.Thread(target=self._listen_loop, daemon=True)
            self.thread.start()

            print(f"CheckInListener: Listening for device check-ins on UDP port {self.port}")

        except Exception as e:
            print(f"CheckInListener: Failed to start: {e}")
            self.running = False

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
                    device_mac = payload.get('device_mac')
                    device_ip = payload.get('ip')

                    if device_mac and device_ip:
                        print(f"CheckInListener: Device {device_mac} checked in from {device_ip}")

                        # Call callback if registered
                        if self.on_device_checkin:
                            try:
                                self.on_device_checkin(device_mac, device_ip)
                            except Exception as e:
                                print(f"CheckInListener: Error in callback: {e}")
                    else:
                        print(f"CheckInListener: Invalid check-in payload: {payload}")

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
