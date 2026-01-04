#!/usr/bin/env python3
"""Test client for chunked upload endpoint.

Usage:
    python3 test_chunked_upload.py <file_to_upload>

This script tests the chunked upload endpoint by uploading a file in small chunks
with CRC32 verification, session management, and resumption support.
"""

import requests
import zlib
import os
import sys

SERVER_URL = "http://localhost:8888"
DEVICE_MAC = "AA:BB:CC:DD:EE:FF"
CHUNK_SIZE = 8192  # 8KB chunks (matches WP client)


def register_test_device():
    """Register test device with server."""
    url = f"{SERVER_URL}/api/device/register"
    data = {
        'mac_address': DEVICE_MAC,
        'wp_version': 'test_v1.0',
        'ep_version': 'test_v1.0',
        'ip_address': '127.0.0.1'
    }

    response = requests.post(url, json=data)
    if response.status_code == 200:
        print(f"Test device registered: {DEVICE_MAC}")
        return True
    else:
        print(f"Failed to register device: {response.status_code} {response.text}")
        return False


def upload_file_chunked(filename, filepath, resume=False):
    """Upload file using chunked endpoint.

    Args:
        filename: Name of file on server
        filepath: Local path to file
        resume: If True, query for existing session and resume
    """

    # Get file size
    file_size = os.path.getsize(filepath)
    print(f"Uploading {filename} ({file_size} bytes) in {CHUNK_SIZE}-byte chunks")

    session_id = None
    offset = 0

    # Query for existing session if resuming
    if resume:
        query_url = f"{SERVER_URL}/logs/upload/{DEVICE_MAC}/session"
        response = requests.get(query_url, params={'filename': filename})

        if response.status_code == 200:
            data = response.json()
            if data.get('session_found'):
                session_id = data.get('session_id')
                offset = data.get('bytes_received', 0)
                print(f"Resuming from offset {offset} (session: {session_id})")
            else:
                print("No existing session found, starting new upload")
        else:
            print(f"Session query failed: {response.status_code}")
            return False

    with open(filepath, 'rb') as f:
        # Seek to resume offset
        f.seek(offset)

        while offset < file_size:
            # Read chunk
            chunk_data = f.read(CHUNK_SIZE)
            chunk_size = len(chunk_data)
            is_last = (offset + chunk_size >= file_size)

            # Calculate CRC32
            crc32 = zlib.crc32(chunk_data) & 0xFFFFFFFF

            # Build headers
            headers = {
                'X-Filename': filename,
                'X-Chunk-Offset': str(offset),
                'X-Chunk-Size': str(chunk_size),
                'X-Total-Size': str(file_size),
                'X-Is-Last-Chunk': 'true' if is_last else 'false',
                'X-Chunk-CRC32': f'{crc32:08X}',
                'Content-Type': 'application/octet-stream'
            }

            if session_id:
                headers['X-Session-ID'] = session_id

            # Upload chunk
            url = f"{SERVER_URL}/logs/upload/{DEVICE_MAC}/chunk"
            response = requests.post(url, data=chunk_data, headers=headers)

            if response.status_code == 200:
                data = response.json()
                session_id = data.get('session_id')
                bytes_received = data.get('bytes_received', offset + chunk_size)
                status = data.get('status', 'ok')

                if status == 'complete':
                    speed = data.get('transfer_speed_mbps', 0)
                    print(f"Upload complete! ({bytes_received} bytes, {speed:.2f} Mbps)")
                else:
                    print(f"Chunk {offset}-{offset+chunk_size} uploaded (session: {session_id})")

                offset += chunk_size
            elif response.status_code == 409:
                # Offset mismatch - server has different state
                print(f"Offset mismatch at {offset}: {response.text}")
                return False
            else:
                print(f"Upload failed: {response.status_code} {response.text}")
                return False

    return True


def test_small_file():
    """Test upload of small file (< 8KB, single chunk)."""
    print("\n=== Test 1: Small file (single chunk) ===")

    # Create small test file
    test_file = "/tmp/test_small.um4"
    with open(test_file, 'wb') as f:
        f.write(b"Test data for small file upload\n" * 100)

    success = upload_file_chunked("test_small.um4", test_file)
    os.remove(test_file)

    return success


def test_medium_file():
    """Test upload of medium file (100KB, multiple chunks)."""
    print("\n=== Test 2: Medium file (multiple chunks) ===")

    # Create medium test file (100KB)
    test_file = "/tmp/test_medium.um4"
    with open(test_file, 'wb') as f:
        for i in range(100 * 1024 // 64):
            f.write(bytes([i % 256]) * 64)

    success = upload_file_chunked("test_medium.um4", test_file)
    os.remove(test_file)

    return success


def test_resume():
    """Test resume functionality by simulating interruption."""
    print("\n=== Test 3: Resume interrupted upload ===")

    # Create test file (50KB)
    test_file = "/tmp/test_resume.um4"
    with open(test_file, 'wb') as f:
        for i in range(50 * 1024 // 64):
            f.write(bytes([i % 256]) * 64)

    # First, upload first 2 chunks manually
    print("Uploading first 2 chunks...")
    file_size = os.path.getsize(test_file)

    with open(test_file, 'rb') as f:
        for chunk_num in range(2):
            offset = chunk_num * CHUNK_SIZE
            chunk_data = f.read(CHUNK_SIZE)
            chunk_size = len(chunk_data)
            crc32 = zlib.crc32(chunk_data) & 0xFFFFFFFF

            headers = {
                'X-Filename': 'test_resume.um4',
                'X-Chunk-Offset': str(offset),
                'X-Chunk-Size': str(chunk_size),
                'X-Total-Size': str(file_size),
                'X-Is-Last-Chunk': 'false',
                'X-Chunk-CRC32': f'{crc32:08X}',
                'Content-Type': 'application/octet-stream'
            }

            url = f"{SERVER_URL}/logs/upload/{DEVICE_MAC}/chunk"
            response = requests.post(url, data=chunk_data, headers=headers)

            if response.status_code != 200:
                print(f"Failed to upload chunk {chunk_num}: {response.status_code}")
                os.remove(test_file)
                return False

            print(f"Uploaded chunk {chunk_num} at offset {offset}")

    # Now resume from where we left off
    print("\nResuming upload...")
    success = upload_file_chunked("test_resume.um4", test_file, resume=True)
    os.remove(test_file)

    return success


def test_variable_chunk_sizes():
    """Test different chunk sizes."""
    print("\n=== Test 4: Variable chunk sizes ===")

    # Test with different chunk sizes
    global CHUNK_SIZE
    original_chunk_size = CHUNK_SIZE

    for size_kb in [4, 8, 16, 32]:
        CHUNK_SIZE = size_kb * 1024
        print(f"\nTesting with {size_kb}KB chunks...")

        # Create test file (100KB)
        test_file = f"/tmp/test_{size_kb}kb_chunks.um4"
        with open(test_file, 'wb') as f:
            for i in range(100 * 1024 // 64):
                f.write(bytes([i % 256]) * 64)

        success = upload_file_chunked(f"test_{size_kb}kb.um4", test_file)
        os.remove(test_file)

        if not success:
            CHUNK_SIZE = original_chunk_size
            return False

    CHUNK_SIZE = original_chunk_size
    return True


def main():
    """Main test runner."""
    # Register test device first
    if not register_test_device():
        print("Failed to register test device - cannot continue")
        return 1

    if len(sys.argv) > 1:
        # Upload user-specified file
        filepath = sys.argv[1]
        filename = os.path.basename(filepath)

        if not os.path.exists(filepath):
            print(f"Error: File not found: {filepath}")
            return 1

        success = upload_file_chunked(filename, filepath)
        return 0 if success else 1

    # Run automated tests
    print("\nRunning automated chunked upload tests...")
    print(f"Server: {SERVER_URL}")
    print(f"Device MAC: {DEVICE_MAC}")

    tests = [
        ("Small file (single chunk)", test_small_file),
        ("Medium file (multiple chunks)", test_medium_file),
        ("Resume functionality", test_resume),
        ("Variable chunk sizes", test_variable_chunk_sizes),
    ]

    results = []
    for name, test_func in tests:
        try:
            success = test_func()
            results.append((name, success))
        except Exception as e:
            print(f"Test failed with exception: {e}")
            import traceback
            traceback.print_exc()
            results.append((name, False))

    # Print results
    print("\n" + "="*60)
    print("Test Results:")
    print("="*60)

    for name, success in results:
        status = "✓ PASS" if success else "✗ FAIL"
        print(f"{status:8} {name}")

    all_passed = all(success for _, success in results)
    print("="*60)
    print(f"Overall: {'ALL TESTS PASSED' if all_passed else 'SOME TESTS FAILED'}")

    return 0 if all_passed else 1


if __name__ == '__main__':
    sys.exit(main())
