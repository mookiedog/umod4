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

from models.database import Database
from http_server import Umod4Server
from gui.main_window import MainWindow


class ServerCallbackBridge(QObject):
    """Bridge to marshal server callbacks from background threads to Qt main thread."""
    device_registered = Signal(object)  # Device object
    transfer_started = Signal(object)  # Transfer object
    transfer_completed = Signal(int)  # Transfer ID
    connection_event = Signal(object)  # Connection object


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

    # Initialize HTTP server
    print(f"Initializing HTTP server on {args.host}:{args.port}")
    server = Umod4Server(database, port=args.port, host=args.host)

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

    # Show window
    window.show()

    # Auto-start server
    print("Starting HTTP server...")
    window._toggle_server()

    # Run application
    print("umod4 Server ready!")
    sys.exit(app.exec())


if __name__ == '__main__':
    main()
