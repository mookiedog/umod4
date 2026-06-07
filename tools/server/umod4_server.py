#!/usr/bin/env python3
"""
umod4 Server - Log file receiver and device management

Desktop application for receiving log files from umod4 motorcycle data loggers.
Supports multiple devices with per-device configuration and storage.

Usage:
    python umod4_server.py [--db PATH]

Requirements:
    - PySide6
    - SQLAlchemy
"""

import sys
import os
import queue
import threading
import argparse
from datetime import datetime
from dataclasses import dataclass
from typing import Optional
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QTimer

# Add current directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from models.database import Database, Device
from models.app_settings import AppSettings
from gui.main_window import MainWindow
from checkin_listener import CheckInListener, CHECKIN_PORT
from device_manager import DeviceManager
from connectivity_checker import ConnectivityChecker


# --- Event types (background threads put these on the queue) ---

@dataclass
class DeviceRegisteredEvent:
    mac_address: str
    display_name: str

@dataclass
class TransferStartedEvent:
    device_mac: str
    filename: str

@dataclass
class TransferCompletedEvent:
    transfer_id: int

@dataclass
class ConnectionEvent:
    device_mac: str
    ip_address: str

@dataclass
class DeviceCheckinEvent:
    device_mac: str
    device_ip: str

@dataclass
class DeviceStatusChangedEvent:
    device_mac: str
    is_online: bool

@dataclass
class DownloadStartedEvent:
    device_mac: str
    filename: str

@dataclass
class DownloadCompletedEvent:
    device_mac: str
    filename: str
    success: bool
    error_msg: Optional[str]


def _run_flash_ep(ip: str, uf2_path: str) -> int:
    """
    CLI mode: upload uf2_path to device at ip, trigger EP reflash, exit with 0/1.
    Does not start the GUI or bind any server ports.
    """
    from device_client import DeviceClient

    if not os.path.isfile(uf2_path):
        print(f"ERROR: file not found: {uf2_path}", file=sys.stderr)
        return 1

    client = DeviceClient(f"http://{ip}")
    filename = os.path.basename(uf2_path)

    print(f"Uploading {uf2_path} to {ip} as {filename} ...")
    ok, session_id, error = client.upload_file(uf2_path, filename)
    if not ok:
        print(f"ERROR: upload failed: {error}", file=sys.stderr)
        return 1
    print("Upload complete.")

    print(f"Triggering EP reflash with {filename} ...")
    ok, error = client.reflash_ep(filename)
    if not ok:
        print(f"ERROR: reflash failed: {error}", file=sys.stderr)
        return 1

    print("EP reflash succeeded.")
    return 0


def main():
    """Main entry point for umod4 server application."""
    parser = argparse.ArgumentParser(description='umod4 Server - Log file receiver')
    parser.add_argument('--db', type=str, default=None,
                       help='Database path (default: platform-specific)')
    parser.add_argument('--flash-ep', metavar='UF2_PATH',
                       help='CLI mode: upload UF2 and reflash EP (requires --ip)')
    parser.add_argument('--ip', metavar='ADDRESS',
                       help='Device IP address for CLI operations')

    args = parser.parse_args()

    if args.flash_ep:
        if not args.ip:
            parser.error('--flash-ep requires --ip <device_ip>')
        sys.exit(_run_flash_ep(args.ip, args.flash_ep))

    # Thread-safe event queue: background threads put events here,
    # a QTimer on the main thread drains and dispatches them.
    # This eliminates all cross-thread Qt signal emission.
    event_queue = queue.Queue()

    # Initialize database
    print(f"Initializing database...")
    database = Database(db_path=args.db)
    print(f"Database: {database.db_path}")

    # Initialize persistent application settings
    app_settings = AppSettings()

    # Initialize device manager (for pull-based downloads)
    print(f"Initializing device manager...")
    device_manager = DeviceManager(database)

    print(f"Initializing check-in listener on UDP port {CHECKIN_PORT}...")
    checkin_listener = CheckInListener()

    # Initialize connectivity checker (actively pings devices every 60 seconds)
    print(f"Initializing connectivity checker...")
    connectivity_checker = ConnectivityChecker(database, check_interval=60)

    # Create Qt application
    app = QApplication(sys.argv)
    app.setApplicationName("umod4 Server")
    app.setOrganizationName("umod4")

    # Create main window
    window = MainWindow(database, connectivity_checker=connectivity_checker, device_manager=device_manager, app_settings=app_settings)

    # --- Background thread callbacks (just put events on the queue) ---

    # Device manager callbacks (called from download threads)
    device_manager.on_download_started = lambda mac, fn: event_queue.put(
        DownloadStartedEvent(device_mac=mac, filename=fn))
    device_manager.on_download_completed = lambda mac, fn, ok, err: event_queue.put(
        DownloadCompletedEvent(device_mac=mac, filename=fn, success=ok, error_msg=err))

    # Check-in listener callback (called from UDP listener thread)
    checkin_listener.set_callback(lambda mac, ip: event_queue.put(
        DeviceCheckinEvent(device_mac=mac, device_ip=ip)))

    # Connectivity checker callback (called from checker thread)
    connectivity_checker.on_status_changed = lambda mac, online: event_queue.put(
        DeviceStatusChangedEvent(device_mac=mac, is_online=online))

    # --- Main thread event dispatcher (QTimer, no cross-thread Qt calls) ---

    def dispatch_events():
        """Drain the event queue and handle each event on the main thread."""
        while True:
            try:
                event = event_queue.get_nowait()
            except queue.Empty:
                break

            if isinstance(event, DeviceRegisteredEvent):
                print(f"New device registered: {event.display_name} ({event.mac_address})")
                window.configure_new_device(event.mac_address)
                window.device_list.refresh_devices()

            elif isinstance(event, TransferStartedEvent):
                print(f"Transfer started: {event.filename} from {event.device_mac}")

            elif isinstance(event, TransferCompletedEvent):
                print(f"Transfer completed: {event.transfer_id}")
                window.transfer_history.refresh_transfers()

            elif isinstance(event, ConnectionEvent):
                print(f"Device connected: {event.device_mac} from {event.ip_address}")

            elif isinstance(event, DeviceCheckinEvent):
                print(f"Device check-in: {event.device_mac} at {event.device_ip}")
                # UDP receipt is proof of connectivity — mark online now, before HTTP GET
                session = database.get_session()
                try:
                    device = session.query(Device).filter_by(mac_address=event.device_mac).first()
                    if device:
                        device.is_online = True
                        device.last_ip = event.device_ip
                        device.last_seen = datetime.utcnow()
                        session.commit()
                except Exception as e:
                    print(f"CheckIn: Error marking device online: {e}")
                finally:
                    session.close()
                # Refresh UI with the updated online state
                window.device_list.refresh_devices()
                # Run device manager in a background thread to avoid blocking UI
                thread = threading.Thread(
                    target=device_manager.handle_device_checkin,
                    args=(event.device_mac, event.device_ip),
                    daemon=True
                )
                thread.start()
                # Force ConnectivityChecker to re-check this device immediately
                # so ep_version, fs_status etc. update right after a reboot
                thread2 = threading.Thread(
                    target=connectivity_checker.check_device_now,
                    args=(event.device_mac,),
                    daemon=True
                )
                thread2.start()

            elif isinstance(event, DeviceStatusChangedEvent):
                window.device_list.refresh_devices()
                if event.is_online:
                    # Device came online — trigger a download check immediately
                    session = database.get_session()
                    try:
                        device = session.query(Device).filter_by(mac_address=event.device_mac).first()
                        device_ip = device.last_ip if device else None
                    finally:
                        session.close()
                    if device_ip:
                        thread = threading.Thread(
                            target=device_manager.handle_device_checkin,
                            args=(event.device_mac, device_ip),
                            daemon=True
                        )
                        thread.start()

            elif isinstance(event, DownloadStartedEvent):
                print(f"Download started: {event.filename} from {event.device_mac}")

            elif isinstance(event, DownloadCompletedEvent):
                print(f"Download completed: {event.filename} from {event.device_mac} "
                      f"(success={event.success})")
                window.transfer_history.refresh_transfers()

    event_timer = QTimer()
    event_timer.timeout.connect(dispatch_events)
    event_timer.start(100)  # Poll every 100ms

    # Show window
    window.show()

    # Queue a synthetic check-in for every known device so that any transfers
    # interrupted by a server restart resume immediately rather than waiting
    # up to 5 minutes for the next UDP heartbeat.
    startup_session = database.get_session()
    try:
        startup_devices = startup_session.query(Device).filter(Device.last_ip.isnot(None)).all()
        for d in startup_devices:
            event_queue.put(DeviceCheckinEvent(device_mac=d.mac_address, device_ip=d.last_ip))
            print(f"Queued startup check-in for {d.display_name} ({d.last_ip})")
    finally:
        startup_session.close()

    print("Starting check-in listener...")
    checkin_listener.start()

    print("Starting connectivity checker...")
    connectivity_checker.start()

    # Run application
    print("umod4 Server ready!")
    print(f"  - Check-in listener: UDP port {CHECKIN_PORT}")
    print(f"  - Connectivity checker: pings devices every {connectivity_checker.check_interval}s")
    print("Waiting for device check-ins...")

    try:
        sys.exit(app.exec())
    finally:
        # Clean up on exit
        print("Shutting down...")
        event_timer.stop()
        connectivity_checker.stop()
        checkin_listener.stop()


if __name__ == '__main__':
    main()
