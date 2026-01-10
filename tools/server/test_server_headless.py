#!/usr/bin/env python3
"""
Headless server test - runs server without GUI for testing.

Usage:
    python test_server_headless.py [--port PORT]
"""

import sys
import os
import argparse
import signal

# Add current directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from models.database import Database
from http_server import Umod4Server


def main():
    """Run server without GUI for testing."""
    parser = argparse.ArgumentParser(description='umod4 Server (headless)')
    parser.add_argument('--port', type=int, default=8080,
                       help='HTTP server port (default: 8080)')
    parser.add_argument('--db', type=str, default=None,
                       help='Database path (default: platform-specific)')

    args = parser.parse_args()

    # Initialize database
    print(f"Initializing database...")
    database = Database(db_path=args.db)
    print(f"Database: {database.db_path}")

    # Initialize HTTP server
    print(f"Initializing HTTP server on port {args.port}")
    server = Umod4Server(database, port=args.port, host='0.0.0.0')

    # Set up event callbacks
    def on_device_registered(device):
        print(f"[EVENT] New device registered: {device.display_name} ({device.mac_address})")

    def on_transfer_started(transfer):
        print(f"[EVENT] Transfer started: {transfer.filename} from {transfer.device_mac}")

    def on_transfer_completed(transfer_id):
        print(f"[EVENT] Transfer completed: ID {transfer_id}")

    def on_connection_event(connection):
        print(f"[EVENT] Device connected: {connection.device_mac} from {connection.ip_address}")

    server.on_device_registered = on_device_registered
    server.on_transfer_started = on_transfer_started
    server.on_transfer_completed = on_transfer_completed
    server.on_connection_event = on_connection_event

    # Start server
    print("Starting HTTP server...")
    server.start()
    print(f"Server running at http://localhost:{args.port}")
    print("Press Ctrl+C to stop")
    print()

    # Handle Ctrl+C gracefully
    def signal_handler(sig, frame):
        print("\nStopping server...")
        server.stop()
        sys.exit(0)

    signal.signal(signal.SIGINT, signal_handler)

    # Keep running
    signal.pause()


if __name__ == '__main__':
    main()
