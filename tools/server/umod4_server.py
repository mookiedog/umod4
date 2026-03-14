#!/usr/bin/env python3
"""
umod4 Server - Log file receiver and device management

Desktop application for receiving log files from umod4 motorcycle data loggers.
Supports multiple devices with per-device configuration and storage.

Usage:
    python umod4_server.py [--port PORT] [--db PATH]

Requirements:
    - PySide6
    - Flask
    - SQLAlchemy
"""

import sys
import os
import queue
import threading
import argparse
from dataclasses import dataclass
from typing import Optional
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QTimer

# Add current directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from models.database import Database, Device
from http_server import Umod4Server
from gui.main_window import MainWindow
from checkin_listener import CheckInListener
from device_manager import DeviceManager
from timeout_monitor import TimeoutMonitor
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


def main():
    """Main entry point for umod4 server application."""
    parser = argparse.ArgumentParser(description='umod4 Server - Log file receiver')
    parser.add_argument('--port', type=int, default=8080,
                       help='HTTP server port (default: 8080)')
    parser.add_argument('--db', type=str, default=None,
                       help='Database path (default: platform-specific)')
    parser.add_argument('--host', type=str, default='0.0.0.0',
                       help='HTTP server host (default: 0.0.0.0)')

    args = parser.parse_args()

    # Thread-safe event queue: background threads put events here,
    # a QTimer on the main thread drains and dispatches them.
    # This eliminates all cross-thread Qt signal emission.
    event_queue = queue.Queue()

    # Initialize database
    print(f"Initializing database...")
    database = Database(db_path=args.db)
    print(f"Database: {database.db_path}")

    # Initialize HTTP server (legacy push-based upload endpoint)
    print(f"Initializing HTTP server on {args.host}:{args.port}")
    server = Umod4Server(database, port=args.port, host=args.host)

    # Initialize device manager (for pull-based downloads)
    print(f"Initializing device manager...")
    device_manager = DeviceManager(database)

    # Initialize check-in listener (UDP port 8081)
    print(f"Initializing check-in listener on UDP port 8081...")
    checkin_listener = CheckInListener(port=8081)

    # Initialize timeout monitor (marks devices offline after 6 minutes without check-in)
    # WP heartbeat is every 5 minutes, so 6 minutes = 1.2x heartbeat interval
    print(f"Initializing timeout monitor...")
    timeout_monitor = TimeoutMonitor(database, timeout_seconds=360, check_interval=30)

    # Initialize connectivity checker (actively pings devices every 60 seconds)
    print(f"Initializing connectivity checker...")
    connectivity_checker = ConnectivityChecker(database, check_interval=60)

    # Create Qt application
    app = QApplication(sys.argv)
    app.setApplicationName("umod4 Server")
    app.setOrganizationName("umod4")

    # Create main window
    window = MainWindow(database, server, connectivity_checker=connectivity_checker)

    # --- Background thread callbacks (just put events on the queue) ---

    # HTTP server callbacks (called from werkzeug server thread)
    def on_server_device_registered(device):
        event_queue.put(DeviceRegisteredEvent(
            mac_address=device.mac_address,
            display_name=device.display_name
        ))

    def on_server_transfer_started(transfer):
        event_queue.put(TransferStartedEvent(
            device_mac=transfer.device_mac,
            filename=transfer.filename
        ))

    def on_server_transfer_completed(transfer_id):
        event_queue.put(TransferCompletedEvent(transfer_id=transfer_id))

    def on_server_connection_event(connection):
        event_queue.put(ConnectionEvent(
            device_mac=connection.device_mac,
            ip_address=connection.ip_address
        ))

    server.on_device_registered = on_server_device_registered
    server.on_transfer_started = on_server_transfer_started
    server.on_transfer_completed = on_server_transfer_completed
    server.on_connection_event = on_server_connection_event

    # Device manager callbacks (called from download threads)
    device_manager.on_download_started = lambda mac, fn: event_queue.put(
        DownloadStartedEvent(device_mac=mac, filename=fn))
    device_manager.on_download_completed = lambda mac, fn, ok, err: event_queue.put(
        DownloadCompletedEvent(device_mac=mac, filename=fn, success=ok, error_msg=err))

    # Check-in listener callback (called from UDP listener thread)
    checkin_listener.set_callback(lambda mac, ip: event_queue.put(
        DeviceCheckinEvent(device_mac=mac, device_ip=ip)))

    # Timeout monitor callback (called from monitor thread)
    timeout_monitor.on_status_changed = lambda mac, online: event_queue.put(
        DeviceStatusChangedEvent(device_mac=mac, is_online=online))

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
                # Run device manager in a background thread to avoid blocking UI
                thread = threading.Thread(
                    target=device_manager.handle_device_checkin,
                    args=(event.device_mac, event.device_ip),
                    daemon=True
                )
                thread.start()

            elif isinstance(event, DeviceStatusChangedEvent):
                window.device_list.refresh_devices()

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

    # Auto-start server and check-in listener
    print("Starting HTTP server...")
    window._toggle_server()

    print("Starting check-in listener...")
    checkin_listener.start()

    print("Starting timeout monitor...")
    timeout_monitor.start()

    print("Starting connectivity checker...")
    connectivity_checker.start()

    # Run application
    print("umod4 Server ready!")
    print("  - HTTP server (legacy push): http://{}:{}".format(args.host, args.port))
    print("  - Check-in listener (pull): UDP port 8081")
    print("  - Timeout monitor: devices offline after 360s without check-in")
    print("  - Connectivity checker: actively pings devices every 60s")
    print("Waiting for device check-ins...")

    try:
        sys.exit(app.exec())
    finally:
        # Clean up on exit
        print("Shutting down...")
        event_timer.stop()
        connectivity_checker.stop()
        timeout_monitor.stop()
        checkin_listener.stop()
        server.stop()


if __name__ == '__main__':
    main()
