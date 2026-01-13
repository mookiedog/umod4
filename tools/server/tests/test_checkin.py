#!/usr/bin/env python3
"""
Simple test script for Phase 6 check-in functionality.

This script listens for UDP check-in notifications and prints received data.
Use this to test WP check-in without starting the full server.

Usage:
    python test_checkin.py
"""

import socket
import json
import sys

def main():
    """Listen for UDP check-in packets on port 8081."""
    port = 8081
    host = '0.0.0.0'

    print(f"UDP Check-In Test Listener")
    print(f"Listening on UDP port {port}...")
    print(f"Waiting for device check-ins...\n")

    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, port))
    sock.settimeout(None)  # Block indefinitely

    try:
        while True:
            # Receive packet
            data, addr = sock.recvfrom(1024)

            print(f"=" * 60)
            print(f"Received packet from {addr[0]}:{addr[1]}")
            print(f"Raw data: {data}")

            # Try to parse as JSON
            try:
                payload = json.loads(data.decode('utf-8'))
                print(f"Parsed JSON:")
                print(f"  Device MAC: {payload.get('device_mac', 'N/A')}")
                print(f"  Device IP:  {payload.get('ip', 'N/A')}")
                print(f"\nFull payload:")
                print(f"  {json.dumps(payload, indent=2)}")
            except json.JSONDecodeError as e:
                print(f"ERROR: Not valid JSON: {e}")
                print(f"Data: {data.decode('utf-8', errors='replace')}")

            print(f"=" * 60)
            print()

    except KeyboardInterrupt:
        print("\n\nShutting down...")
        sock.close()
        sys.exit(0)

if __name__ == '__main__':
    main()
