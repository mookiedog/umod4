"""Timeout monitor for umod4 devices.

This module monitors device check-in timestamps and marks devices offline
if they haven't checked in within a configurable timeout period.

Unlike ConnectivityChecker (which actively polls devices), TimeoutMonitor
is passive - it only monitors check-in timestamps and marks devices offline
when the timeout expires.
"""

import threading
import time
from datetime import datetime, timedelta
from typing import Optional
from models.database import Database, Device


class TimeoutMonitor:
    """Monitors device check-in timestamps and marks devices offline on timeout."""

    def __init__(self, database: Database, timeout_seconds: int = 360, check_interval: int = 30, verify_delay: int = 5):
        """Initialize timeout monitor.

        Args:
            database: Database instance
            timeout_seconds: Seconds since last check-in before marking offline (default: 360 = 6 minutes)
            check_interval: Seconds between timeout checks (default: 30)
            verify_delay: Seconds to wait before re-checking a timed-out device (default: 5)
        """
        self.database = database
        self.timeout_seconds = timeout_seconds
        self.check_interval = check_interval
        self.verify_delay = verify_delay
        self.running = False
        self.thread = None
        self.on_status_changed = None  # Callback(device_mac, is_online)

    def start(self):
        """Start the timeout monitor in a background thread."""
        if self.running:
            return

        self.running = True
        self.thread = threading.Thread(target=self._check_loop, daemon=True)
        self.thread.start()
        print(f"TimeoutMonitor: Started (timeout={self.timeout_seconds}s, checking every {self.check_interval}s, verify_delay={self.verify_delay}s)")

    def stop(self):
        """Stop the timeout monitor."""
        if not self.running:
            return

        self.running = False
        if self.thread:
            self.thread.join(timeout=5)

        print("TimeoutMonitor: Stopped")

    def _check_loop(self):
        """Main timeout checking loop (runs in background thread)."""
        while self.running:
            try:
                self._check_all_devices()
            except Exception as e:
                print(f"TimeoutMonitor: Error in check loop: {e}")

            # Sleep for check_interval seconds (check running flag periodically)
            for _ in range(self.check_interval):
                if not self.running:
                    break
                time.sleep(1)

    def _check_all_devices(self):
        """Check all devices for timeout."""
        session = self.database.get_session()
        try:
            # Get all devices that are currently marked as online
            devices = session.query(Device).filter_by(is_online=True).all()

            now = datetime.utcnow()
            timeout_threshold = now - timedelta(seconds=self.timeout_seconds)

            for device in devices:
                # Check if device has timed out
                if device.last_seen and device.last_seen < timeout_threshold:
                    # Device appears to have timed out - verify before marking offline
                    print(f"TimeoutMonitor: Device {device.display_name} ({device.mac_address}) "
                          f"appears timed out (last seen: {device.last_seen}), verifying...")

                    # Wait a bit and check one more time
                    time.sleep(self.verify_delay)

                    # Refresh the device from database to get latest state
                    session.expire(device)
                    session.refresh(device)

                    # Re-check with fresh data
                    now_after_verify = datetime.utcnow()
                    timeout_threshold_verify = now_after_verify - timedelta(seconds=self.timeout_seconds)

                    if device.last_seen and device.last_seen < timeout_threshold_verify:
                        # Still timed out after verification - mark offline
                        device.is_online = False
                        session.commit()

                        print(f"TimeoutMonitor: Device {device.display_name} ({device.mac_address}) "
                              f"confirmed timed out (last seen: {device.last_seen})")

                        # Call callback if registered
                        if self.on_status_changed:
                            try:
                                self.on_status_changed(device.mac_address, False)
                            except Exception as e:
                                print(f"TimeoutMonitor: Error in callback: {e}")
                    else:
                        # Device checked in during verification window - still online
                        print(f"TimeoutMonitor: Device {device.display_name} ({device.mac_address}) "
                              f"recovered during verification (last seen: {device.last_seen})")

        finally:
            session.close()
