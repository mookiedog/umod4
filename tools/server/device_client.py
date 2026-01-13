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
