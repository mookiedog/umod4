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
from PySide6.QtCore import Qt

# Add current directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from models.database import Database
from http_server import Umod4Server
from gui.main_window import MainWindow


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

    # Create main window
    window = MainWindow(database, server)

    # Set up server callbacks for GUI updates
    def on_device_registered(device):
        """Called when new device registers."""
        print(f"New device registered: {device.display_name} ({device.mac_address})")
        window.device_list.refresh_devices()

    def on_transfer_started(transfer):
        """Called when transfer starts."""
        print(f"Transfer started: {transfer.filename} from {transfer.device_mac}")

    def on_transfer_completed(transfer_id):
        """Called when transfer completes."""
        print(f"Transfer completed: {transfer_id}")
        window.transfer_history.refresh_transfers()

    def on_connection_event(connection):
        """Called on connection event."""
        print(f"Device connected: {connection.device_mac} from {connection.ip_address}")

    server.on_device_registered = on_device_registered
    server.on_transfer_started = on_transfer_started
    server.on_transfer_completed = on_transfer_completed
    server.on_connection_event = on_connection_event

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
