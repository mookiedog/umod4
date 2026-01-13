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
import argparse
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import Qt, QObject, Signal

# Add current directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from models.database import Database, Device
from http_server import Umod4Server
from gui.main_window import MainWindow
from checkin_listener import CheckInListener
from device_manager import DeviceManager
from timeout_monitor import TimeoutMonitor
from connectivity_checker import ConnectivityChecker


class ServerCallbackBridge(QObject):
    """Bridge to marshal server callbacks from background threads to Qt main thread."""
    device_registered = Signal(object)  # Device object
    transfer_started = Signal(object)  # Transfer object
    transfer_completed = Signal(int)  # Transfer ID
    connection_event = Signal(object)  # Connection object
    device_checkin = Signal(str, str)  # device_mac, device_ip
    device_status_changed = Signal(str, bool)  # device_mac, is_online


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
    # WP heartbeat is every 5 minutes, so 6 minutes = 1.2× heartbeat interval
    print(f"Initializing timeout monitor...")
    timeout_monitor = TimeoutMonitor(database, timeout_seconds=360, check_interval=30)

    # Initialize connectivity checker (actively pings devices every 60 seconds)
    print(f"Initializing connectivity checker...")
    connectivity_checker = ConnectivityChecker(database, check_interval=60)

    # Create Qt application
    app = QApplication(sys.argv)
    app.setApplicationName("umod4 Server")
    app.setOrganizationName("umod4")

    # Create callback bridge to marshal server callbacks to Qt thread
    bridge = ServerCallbackBridge()

    # Create main window
    window = MainWindow(database, server)

    # Connect bridge signals to window slots (runs in Qt main thread)
    def on_device_registered(device):
        """Called in Qt main thread when new device registers."""
        print(f"New device registered: {device.display_name} ({device.mac_address})")

        # Show configuration dialog for new device
        window.configure_new_device(device.mac_address)

        window.device_list.refresh_devices()

    def on_transfer_started(transfer):
        """Called in Qt main thread when transfer starts."""
        print(f"Transfer started: {transfer.filename} from {transfer.device_mac}")

    def on_transfer_completed(transfer_id):
        """Called in Qt main thread when transfer completes."""
        print(f"Transfer completed: {transfer_id}")
        window.transfer_history.refresh_transfers()

    def on_connection_event(connection):
        """Called in Qt main thread on connection event."""
        print(f"Device connected: {connection.device_mac} from {connection.ip_address}")

    bridge.device_registered.connect(on_device_registered)
    bridge.transfer_started.connect(on_transfer_started)
    bridge.transfer_completed.connect(on_transfer_completed)
    bridge.connection_event.connect(on_connection_event)

    # Set up server callbacks (called from server thread, emits signals)
    # Note: We must extract data from ORM objects before emitting, as they may be
    # detached from their session by the time the signal is processed in the Qt thread
    def emit_device_registered(device):
        # Create a detached copy with just the data we need
        from types import SimpleNamespace
        device_data = SimpleNamespace(
            mac_address=device.mac_address,
            display_name=device.display_name
        )
        bridge.device_registered.emit(device_data)

    def emit_transfer_started(transfer):
        from types import SimpleNamespace
        transfer_data = SimpleNamespace(
            device_mac=transfer.device_mac,
            filename=transfer.filename
        )
        bridge.transfer_started.emit(transfer_data)

    def emit_connection_event(connection):
        from types import SimpleNamespace
        connection_data = SimpleNamespace(
            device_mac=connection.device_mac,
            ip_address=connection.ip_address
        )
        bridge.connection_event.emit(connection_data)

    server.on_device_registered = emit_device_registered
    server.on_transfer_started = emit_transfer_started
    server.on_transfer_completed = lambda transfer_id: bridge.transfer_completed.emit(transfer_id)
    server.on_connection_event = emit_connection_event

    # Set up device manager callbacks (called from device manager thread)
    def emit_download_started(device_mac, filename):
        print(f"Download started: {filename} from {device_mac}")
        # Note: transfer record is created by device_manager, will be shown in UI

    def emit_download_completed(device_mac, filename, success, error_msg):
        print(f"Download completed: {filename} from {device_mac} (success={success})")
        # Refresh transfer history in UI
        bridge.transfer_completed.emit(0)  # Dummy transfer ID to trigger refresh

    device_manager.on_download_started = emit_download_started
    device_manager.on_download_completed = emit_download_completed

    # Set up check-in listener callback (called from listener thread)
    def on_device_checkin(device_mac, device_ip):
        print(f"Device check-in: {device_mac} at {device_ip}")
        # Emit signal to Qt thread
        bridge.device_checkin.emit(device_mac, device_ip)

    checkin_listener.set_callback(on_device_checkin)

    # Connect check-in signal to device manager (runs in Qt main thread)
    def handle_checkin_in_qt_thread(device_mac, device_ip):
        # Run device manager in a background thread to avoid blocking UI
        import threading
        thread = threading.Thread(
            target=device_manager.handle_device_checkin,
            args=(device_mac, device_ip),
            daemon=True
        )
        thread.start()

    bridge.device_checkin.connect(handle_checkin_in_qt_thread)

    # Set up timeout monitor callback (called from monitor thread)
    def on_device_status_changed(device_mac, is_online):
        # Emit signal to Qt thread
        bridge.device_status_changed.emit(device_mac, is_online)

    timeout_monitor.on_status_changed = on_device_status_changed

    # Connect status change signal to refresh device list (runs in Qt main thread)
    def handle_status_changed_in_qt_thread(device_mac, is_online):
        # Refresh GUI to show updated online status
        window.device_list.refresh_devices()

        # Note: TimeoutMonitor only marks devices offline (never online)
        # Devices become online when they send check-ins (handled by handle_checkin_in_qt_thread)

    bridge.device_status_changed.connect(handle_status_changed_in_qt_thread)

    # Set up connectivity checker callback (called from checker thread)
    connectivity_checker.on_status_changed = on_device_status_changed

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
        connectivity_checker.stop()
        timeout_monitor.stop()
        checkin_listener.stop()
        server.stop()


if __name__ == '__main__':
    main()
