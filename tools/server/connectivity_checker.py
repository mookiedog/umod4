"""Periodic connectivity checker for umod4 devices.

This module periodically pings devices to verify they are still connected
and updates their online status in the database.
"""

import threading
import time
from datetime import datetime
from typing import Optional
from device_client import DeviceClient
from models.database import Database, Device


class ConnectivityChecker:
    """Periodically checks device connectivity and updates status."""

    def __init__(self, database: Database, check_interval: int = 10):
        """Initialize connectivity checker.

        Args:
            database: Database instance
            check_interval: Seconds between connectivity checks (default: 10)
        """
        self.database = database
        self.check_interval = check_interval
        self.running = False
        self.thread = None
        self.on_status_changed = None  # Callback(device_mac, is_online)

    def start(self):
        """Start the connectivity checker in a background thread."""
        if self.running:
            return

        self.running = True
        self.thread = threading.Thread(target=self._check_loop, daemon=True)
        self.thread.start()
        print(f"ConnectivityChecker: Started (checking every {self.check_interval}s)")

    def stop(self):
        """Stop the connectivity checker."""
        if not self.running:
            return

        self.running = False
        if self.thread:
            self.thread.join(timeout=5)

        print("ConnectivityChecker: Stopped")

    def _check_loop(self):
        """Main connectivity checking loop (runs in background thread)."""
        while self.running:
            try:
                self._check_all_devices()
            except Exception as e:
                print(f"ConnectivityChecker: Error in check loop: {e}")

            # Sleep for check_interval seconds (check running flag periodically)
            for _ in range(self.check_interval):
                if not self.running:
                    break
                time.sleep(1)

    def _check_all_devices(self):
        """Check connectivity for all devices."""
        session = self.database.get_session()
        try:
            # Get all devices
            devices = session.query(Device).all()

            for device in devices:
                # Skip devices with no known IP
                if not device.last_ip:
                    continue

                # Check if device is reachable and get device info
                was_online = device.is_online
                is_online, info = self._check_device(device.last_ip)

                # Extract fields from info
                fs_status = info.get('fs_status') if info else None
                fs_message = info.get('fs_message') if info else None

                # Update status if changed
                if is_online != was_online:
                    device.is_online = is_online
                    device.filesystem_status = fs_status
                    device.filesystem_message = fs_message
                    if is_online:
                        device.last_seen = datetime.utcnow()
                        # Update version info when coming online
                        self._update_version_info(device, info)
                    session.commit()

                    print(f"ConnectivityChecker: Device {device.display_name} ({device.mac_address}) "
                          f"is now {'online' if is_online else 'offline'} (fs: {fs_status})")

                    # Call callback if registered
                    if self.on_status_changed:
                        try:
                            self.on_status_changed(device.mac_address, is_online)
                        except Exception as e:
                            print(f"ConnectivityChecker: Error in callback: {e}")
                elif is_online:
                    # Device still online, update filesystem status and version if changed
                    needs_commit = False
                    if device.filesystem_status != fs_status:
                        device.filesystem_status = fs_status
                        device.filesystem_message = fs_message
                        device.last_seen = datetime.utcnow()
                        needs_commit = True
                        print(f"ConnectivityChecker: Device {device.display_name} filesystem status changed to {fs_status}")
                    # Always update version info for online devices (may have changed after reflash)
                    if self._update_version_info(device, info):
                        needs_commit = True
                    if needs_commit:
                        session.commit()

        finally:
            session.close()

    def _check_device(self, device_ip: str) -> tuple:
        """Check if device is online and get device info.

        Args:
            device_ip: Device IP address

        Returns:
            Tuple of (is_online: bool, info: dict or None)
        """
        try:
            client = DeviceClient(device_ip, timeout=5)
            # Get device info (includes filesystem status and version)
            info = client.get_device_info()
            if info:
                return (True, info)
            else:
                return (False, None)
        except Exception as e:
            # Device is offline or unreachable
            return (False, None)

    def _update_version_info(self, device: Device, info: dict) -> bool:
        """Update device version info if changed.

        Args:
            device: Device model instance
            info: Device info dict from API

        Returns:
            True if version info was updated, False otherwise
        """
        if not info:
            return False

        import json
        updated = False

        if 'wp_version' in info:
            wp_ver = info['wp_version']
            if isinstance(wp_ver, dict):
                new_wp_version = json.dumps(wp_ver)
            else:
                new_wp_version = wp_ver
            if device.wp_version != new_wp_version:
                device.wp_version = new_wp_version
                updated = True
                print(f"ConnectivityChecker: Device {device.display_name} wp_version updated to {new_wp_version}")

        if 'ep_version' in info:
            new_ep_version = info.get('ep_version')
            if device.ep_version != new_ep_version:
                device.ep_version = new_ep_version
                updated = True
                print(f"ConnectivityChecker: Device {device.display_name} ep_version updated to {new_ep_version}")

        return updated
