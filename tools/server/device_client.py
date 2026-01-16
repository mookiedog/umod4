"""HTTP client for pulling log files from umod4 devices.

This module implements the pull-based log retrieval system where the server
acts as an HTTP client to download logs from the device's HTTP server.
"""

import requests
import hashlib
import os
from datetime import datetime
from typing import List, Dict, Optional, Tuple


class DeviceClient:
    """HTTP client for communicating with umod4 device."""

    def __init__(self, device_ip: str, timeout: int = 30):
        """Initialize device client.

        Args:
            device_ip: IP address of device (e.g., "192.168.1.150" or "motorcycle.local")
            timeout: HTTP request timeout in seconds
        """
        self.device_ip = device_ip
        self.timeout = timeout
        self.base_url = f"http://{device_ip}"

    def get_device_info(self) -> Optional[Dict]:
        """Get device information via /api/info endpoint.

        Returns:
            Dictionary with device info (mac, version, uptime, etc.) or None on error
        """
        try:
            response = requests.get(
                f"{self.base_url}/api/info",
                timeout=self.timeout
            )
            response.raise_for_status()
            return response.json()
        except Exception as e:
            print(f"Error getting device info from {self.device_ip}: {e}")
            return None

    def list_log_files(self) -> Optional[List[Dict]]:
        """List available log files on device via /api/list endpoint.

        Returns:
            List of dicts with filename and size, or None on error
            Example: [{"filename": "ride_001.um4", "size": 5242880}, ...]
        """
        try:
            response = requests.get(
                f"{self.base_url}/api/list",
                timeout=self.timeout
            )
            response.raise_for_status()
            data = response.json()
            return data.get('files', [])
        except Exception as e:
            print(f"Error listing files from {self.device_ip}: {e}")
            return None

    def download_log_file(
        self,
        filename: str,
        destination_path: str,
        progress_callback=None
    ) -> Tuple[bool, Optional[str]]:
        """Download a log file from device.

        Args:
            filename: Name of file to download (e.g., "ride_001.um4")
            destination_path: Local path to save file
            progress_callback: Optional callback(bytes_downloaded, total_bytes)

        Returns:
            Tuple of (success: bool, error_message: Optional[str])
        """
        try:
            url = f"{self.base_url}/logs/{filename}"
            response = requests.get(url, stream=True, timeout=self.timeout)
            response.raise_for_status()

            # Get total file size from Content-Length header
            total_size = int(response.headers.get('content-length', 0))

            # Download file in chunks
            bytes_downloaded = 0
            chunk_size = 65536  # 64KB chunks

            with open(destination_path, 'wb') as f:
                for chunk in response.iter_content(chunk_size=chunk_size):
                    if chunk:
                        f.write(chunk)
                        bytes_downloaded += len(chunk)

                        if progress_callback:
                            progress_callback(bytes_downloaded, total_size)

            # Verify file size matches
            actual_size = os.path.getsize(destination_path)
            if total_size > 0 and actual_size != total_size:
                error_msg = f"Size mismatch: expected {total_size}, got {actual_size}"
                os.remove(destination_path)  # Delete incomplete file
                return False, error_msg

            return True, None

        except Exception as e:
            error_msg = f"Error downloading {filename}: {e}"
            print(error_msg)
            # Clean up partial file
            if os.path.exists(destination_path):
                os.remove(destination_path)
            return False, error_msg

    def calculate_file_sha256(self, filepath: str) -> str:
        """Calculate SHA256 hash of a file.

        Args:
            filepath: Path to file

        Returns:
            SHA256 hash as hex string
        """
        sha256 = hashlib.sha256()
        with open(filepath, 'rb') as f:
            while True:
                chunk = f.read(65536)
                if not chunk:
                    break
                sha256.update(chunk)
        return sha256.hexdigest()

    def get_file_sha256(self, filename: str) -> Optional[str]:
        """Get SHA-256 hash for a file from device.

        Args:
            filename: Name of file to get hash for (e.g., "ride_001.um4")

        Returns:
            SHA-256 hash as hex string, or None on error
        """
        try:
            response = requests.get(
                f"{self.base_url}/api/sha256/{filename}",
                timeout=self.timeout
            )
            response.raise_for_status()
            data = response.json()

            # Check for error response
            if 'error' in data:
                print(f"Device returned error for SHA-256 request: {data['error']}")
                return None

            return data.get('sha256')
        except Exception as e:
            print(f"Error getting SHA-256 from {self.device_ip}: {e}")
            return None

    def delete_log_file(self, filename: str) -> Tuple[bool, Optional[str]]:
        """Delete a log file from device.

        Args:
            filename: Name of file to delete (e.g., "ride_001.um4")

        Returns:
            Tuple of (success: bool, error_message: Optional[str])
        """
        try:
            response = requests.get(
                f"{self.base_url}/api/delete/{filename}",
                timeout=self.timeout
            )
            response.raise_for_status()
            data = response.json()

            if data.get('success'):
                return True, None
            else:
                error_msg = data.get('error', 'Unknown error')
                return False, error_msg

        except Exception as e:
            error_msg = f"Error deleting {filename}: {e}"
            print(error_msg)
            return False, error_msg

    def ping(self) -> bool:
        """Check if device is reachable.

        Returns:
            True if device responds to /api/info, False otherwise
        """
        try:
            response = requests.get(
                f"{self.base_url}/api/info",
                timeout=5  # Short timeout for ping
            )
            return response.status_code == 200
        except:
            return False

    def upload_file(
        self,
        source_path: str,
        destination_filename: str,
        chunk_size: int = 65536,
        progress_callback=None
    ) -> Tuple[bool, Optional[str], Optional[str]]:
        """Upload a file to device with chunking and SHA256 verification.

        Args:
            source_path: Local path to file to upload
            destination_filename: Name to save as on device (stored in /uploads/)
            chunk_size: Size of chunks to send (default 64KB)
            progress_callback: Optional callback(bytes_sent, total_bytes)

        Returns:
            Tuple of (success: bool, sha256: Optional[str], error_message: Optional[str])
        """
        try:
            # Validate source file
            if not os.path.exists(source_path):
                return False, None, f"Source file not found: {source_path}"

            total_size = os.path.getsize(source_path)
            if total_size == 0:
                return False, None, "Source file is empty"

            # Calculate SHA256 of source file
            print(f"Calculating SHA-256 of source file...")
            source_sha256 = self.calculate_file_sha256(source_path)
            print(f"Source SHA-256: {source_sha256}")

            # Generate session ID
            import uuid
            session_id = str(uuid.uuid4())

            # Upload file in chunks
            bytes_sent = 0
            chunk_offset = 0

            with open(source_path, 'rb') as f:
                while bytes_sent < total_size:
                    # Read chunk
                    chunk = f.read(chunk_size)
                    if not chunk:
                        break

                    # Prepare headers
                    headers = {
                        'X-Session-ID': session_id,
                        'X-Filename': destination_filename,
                        'X-Total-Size': str(total_size),
                        'X-Chunk-Size': str(chunk_size),
                        'X-Chunk-Offset': str(chunk_offset),
                        'X-Is-Last-Chunk': 'true' if (bytes_sent + len(chunk) >= total_size) else 'false',
                        'Content-Type': 'application/octet-stream'
                    }

                    # Calculate CRC32 for this chunk (optional but recommended)
                    import zlib
                    chunk_crc32 = zlib.crc32(chunk) & 0xFFFFFFFF
                    headers['X-Chunk-CRC32'] = f"{chunk_crc32:08x}"

                    # Send chunk
                    response = requests.post(
                        f"{self.base_url}/api/upload",
                        data=chunk,
                        headers=headers,
                        timeout=self.timeout
                    )

                    if response.status_code != 200:
                        error_msg = f"Upload failed: HTTP {response.status_code}"
                        try:
                            error_data = response.json()
                            if 'error' in error_data:
                                error_msg = f"Upload failed: {error_data['error']}"
                        except:
                            pass
                        return False, None, error_msg

                    # Update progress
                    bytes_sent += len(chunk)
                    chunk_offset += len(chunk)

                    if progress_callback:
                        progress_callback(bytes_sent, total_size)

                    # Check if upload is complete
                    try:
                        response_data = response.json()
                        if response_data.get('success'):
                            if bytes_sent >= total_size:
                                print(f"Upload complete: {bytes_sent} bytes sent")
                                break
                    except:
                        pass

            # Verify upload was complete
            if bytes_sent < total_size:
                return False, None, f"Upload incomplete: {bytes_sent}/{total_size} bytes sent"

            print(f"Upload complete, SHA-256 should match: {source_sha256}")
            return True, source_sha256, None

        except Exception as e:
            error_msg = f"Error uploading file: {e}"
            print(error_msg)
            return False, None, error_msg

    def get_upload_session_status(self, session_id: str) -> Optional[Dict]:
        """Get status of an upload session for resumption.

        Args:
            session_id: Session UUID

        Returns:
            Dictionary with session status or None on error
            Example: {"session_id": "...", "filename": "...", "bytes_received": 1234, "total_size": 5678}
        """
        try:
            response = requests.get(
                f"{self.base_url}/api/upload/session?session_id={session_id}",
                timeout=self.timeout
            )
            response.raise_for_status()
            data = response.json()

            # Check for error response
            if 'error' in data:
                print(f"Device returned error for session query: {data['error']}")
                return None

            return data
        except Exception as e:
            print(f"Error getting upload session status: {e}")
            return None
