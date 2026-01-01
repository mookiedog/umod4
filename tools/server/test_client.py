#!/usr/bin/env python3
"""
Test client to simulate umod4 WP device connecting to server.

This script simulates a WP device registering with the server and uploading log files.
Useful for testing the server without real hardware.

Usage:
    python test_client.py [--server SERVER] [--mac MAC] [--count COUNT]
"""

import requests
import argparse
import time
import os
import random
from datetime import datetime


def generate_dummy_log_file(filename, size_mb=5):
    """Generate a dummy .um4 log file for testing.

    Args:
        filename: Output filename
        size_mb: Size in megabytes (default: 5)
    """
    with open(filename, 'wb') as f:
        # Write random data
        chunk_size = 1024 * 1024  # 1MB chunks
        for _ in range(size_mb):
            f.write(os.urandom(chunk_size))


def register_device(server_url, mac_address, wp_version='1.0.0', ep_version='1.0.0'):
    """Register device with server.

    Args:
        server_url: Server base URL (e.g., http://localhost:8080)
        mac_address: Device MAC address
        wp_version: WP firmware version
        ep_version: EP firmware version

    Returns:
        dict: Server response
    """
    url = f"{server_url}/api/device/register"

    payload = {
        'mac_address': mac_address,
        'wp_version': wp_version,
        'ep_version': ep_version,
        'ip_address': '192.168.1.100',
        'battery_voltage': 12.6,
        'sd_free_mb': 1024
    }

    response = requests.post(url, json=payload)
    response.raise_for_status()

    return response.json()


def list_uploaded_logs(server_url, mac_address):
    """Get list of already uploaded logs.

    Args:
        server_url: Server base URL
        mac_address: Device MAC address

    Returns:
        list: List of uploaded filenames
    """
    url = f"{server_url}/logs/list/{mac_address}"

    response = requests.get(url)
    response.raise_for_status()

    return response.json()


def upload_log_file(server_url, mac_address, filename):
    """Upload a log file to server.

    Args:
        server_url: Server base URL
        mac_address: Device MAC address
        filename: Path to log file to upload

    Returns:
        dict: Server response
    """
    url = f"{server_url}/logs/upload/{mac_address}"

    with open(filename, 'rb') as f:
        headers = {
            'X-Filename': os.path.basename(filename)
        }

        response = requests.post(url, data=f, headers=headers)
        response.raise_for_status()

        return response.json()


def main():
    """Main test client."""
    parser = argparse.ArgumentParser(description='Test client for umod4 server')
    parser.add_argument('--server', type=str, default='http://localhost:8080',
                       help='Server URL (default: http://localhost:8080)')
    parser.add_argument('--mac', type=str, default='28:cd:c1:0a:4b:2c',
                       help='Device MAC address (default: 28:cd:c1:0a:4b:2c)')
    parser.add_argument('--count', type=int, default=3,
                       help='Number of log files to upload (default: 3)')
    parser.add_argument('--size', type=int, default=5,
                       help='Size of each log file in MB (default: 5)')

    args = parser.parse_args()

    print(f"umod4 Test Client")
    print(f"=================")
    print(f"Server: {args.server}")
    print(f"Device MAC: {args.mac}")
    print(f"Files to upload: {args.count}")
    print()

    try:
        # Step 1: Register device
        print("Step 1: Registering device...")
        response = register_device(args.server, args.mac)
        print(f"  Device registered: {response['display_name']}")
        print(f"  Upload URL: {response['log_upload_path']}")
        print()

        # Step 2: Check what's already uploaded
        print("Step 2: Checking uploaded files...")
        uploaded_files = list_uploaded_logs(args.server, args.mac)
        print(f"  Already uploaded: {len(uploaded_files)} files")
        for filename in uploaded_files:
            print(f"    - {filename}")
        print()

        # Step 3: Generate and upload test files
        print(f"Step 3: Generating and uploading {args.count} test files...")

        for i in range(args.count):
            # Generate unique filename
            timestamp = int(time.time()) + i
            filename = f"test_log_{timestamp}.um4"

            print(f"  [{i+1}/{args.count}] Generating {filename} ({args.size} MB)...")
            generate_dummy_log_file(filename, args.size)

            print(f"  [{i+1}/{args.count}] Uploading {filename}...")
            start_time = time.time()

            response = upload_log_file(args.server, args.mac, filename)

            elapsed = time.time() - start_time
            speed_mbps = response.get('transfer_speed_mbps', 0)

            print(f"  [{i+1}/{args.count}] Upload complete!")
            print(f"    Status: {response['status']}")
            print(f"    Saved to: {response['saved_to']}")
            print(f"    Transfer speed: {speed_mbps:.2f} MB/s")
            print(f"    Time elapsed: {elapsed:.2f} seconds")

            # Clean up temp file
            os.remove(filename)

            # Small delay between uploads
            if i < args.count - 1:
                time.sleep(1)

            print()

        print("Test complete!")

    except requests.exceptions.ConnectionError:
        print(f"ERROR: Could not connect to server at {args.server}")
        print("Make sure the server is running.")
        return 1

    except requests.exceptions.HTTPError as e:
        print(f"ERROR: HTTP error: {e}")
        print(f"Response: {e.response.text}")
        return 1

    except Exception as e:
        print(f"ERROR: {e}")
        return 1

    return 0


if __name__ == '__main__':
    exit(main())
