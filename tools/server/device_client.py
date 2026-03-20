"""HTTP client for pulling log files from umod4 devices.

This module implements the pull-based log retrieval system where the server
acts as an HTTP client to download logs from the device's HTTP server.
"""

import requests
import hashlib
import os
import time
from datetime import datetime
from typing import List, Dict, Optional, Tuple


class DeviceClient:
    """HTTP client for communicating with umod4 device."""

    def __init__(self, device_ip: str, timeout: int = 5):
        """Initialize device client.

        Args:
            device_ip: IP address of device (e.g., "192.168.1.150" or "motorcycle.local")
            timeout: HTTP request timeout in seconds
        """
        self.device_ip = device_ip
        self.timeout = timeout
        self.base_url = f"http://{device_ip}"
        # Session reuses TCP connections (HTTP keep-alive) across chunk downloads.
        # urllib3 reconnects transparently if the device closes an idle connection.
        self._session = requests.Session()

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

    def get_system_info(self) -> Optional[Dict]:
        """Get system build information via /api/system endpoint.

        Returns:
            Dictionary with build info (GH=git hash, BT=build time) or None on error
            Example: {"GH": "abc1234", "BT": "2024-01-21 10:30:00"}
        """
        try:
            response = requests.get(
                f"{self.base_url}/api/system",
                timeout=self.timeout
            )
            response.raise_for_status()
            return response.json()
        except Exception as e:
            print(f"Error getting system info from {self.device_ip}: {e}")
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

    # Chunk size must match CHUNK_DOWNLOAD_MAX_SIZE in WP/src/fs_custom.cpp
    CHUNK_DOWNLOAD_SIZE = 16 * 1024  # TEST: doubled from 8KB to match larger TCP window
    # Write buffering: accumulate chunks before flushing to disk.
    # Without this, the per-chunk write syscall (~3-5ms in WSL2) adds directly to
    # the gap time between receiving a chunk and sending the next GET request.
    # On interruption, at most WRITE_FLUSH_SIZE bytes must be re-downloaded.
    WRITE_FLUSH_SIZE = 1 * 1024 * 1024  # 1 MB

    def download_log_file(
        self,
        filename: str,
        destination_path: str,
        progress_callback=None
    ) -> Tuple[bool, Optional[str]]:
        """Download a log file from device using resumable chunked transfer.

        Each chunk is an independent GET /api/chunks/<filename>/<offset> request.
        The device returns the chunk data with an X-Chunk-SHA256 header for
        integrity checking.  On failure the partial file is preserved as
        <destination_path>.partial so the next call resumes from the last
        successfully written chunk boundary.

        Args:
            filename: Name of file to download (e.g., "log_16.um4")
            destination_path: Local path for the completed file
            progress_callback: Optional callback(bytes_downloaded, total_bytes)

        Returns:
            Tuple of (success: bool, error_message: Optional[str])
        """
        partial_path = destination_path + '.partial'

        # Resume from last complete chunk boundary if a partial file exists
        resume_offset = 0
        if os.path.exists(partial_path):
            raw_size = os.path.getsize(partial_path)
            resume_offset = (raw_size // self.CHUNK_DOWNLOAD_SIZE) * self.CHUNK_DOWNLOAD_SIZE
            if resume_offset > 0:
                print(f"  Resuming {filename} from byte {resume_offset:,}")
            elif raw_size > 0:
                # Partial file smaller than one chunk — discard and restart
                resume_offset = 0

        total_size = 0
        bytes_downloaded = resume_offset

        try:
            open_mode = 'ab' if resume_offset > 0 else 'wb'
            with open(partial_path, open_mode) as f:
                # Truncate to the last clean chunk boundary when resuming
                if resume_offset > 0:
                    f.seek(resume_offset)
                    f.truncate()

                write_buffer = []
                write_buffer_bytes = 0

                MAX_CHUNK_RETRIES = 8
                CHUNK_RETRY_DELAY = 3.0  # seconds between retries

                while True:
                    url = f"{self.base_url}/api/chunks/{filename}/{bytes_downloaded}"

                    # Retry loop for transient connection errors (IncompleteRead, etc.)
                    response = None
                    chunk_data = None
                    last_error = None
                    for attempt in range(MAX_CHUNK_RETRIES):
                        try:
                            t_start = time.monotonic()
                            response = self._session.get(url, stream=True, timeout=(10, 60))
                            t_headers = time.monotonic()
                            chunk_data = response.content
                            t_done = time.monotonic()
                            t_get  = int((t_headers - t_start)  * 1000)
                            t_recv = int((t_done    - t_headers) * 1000)
                            print(f"CHK off={bytes_downloaded} get={t_get}ms recv={t_recv}ms total={t_get+t_recv}ms")
                            last_error = None
                            break
                        except Exception as e:
                            last_error = e
                            print(f"  Chunk retry {attempt + 1}/{MAX_CHUNK_RETRIES} at offset "
                                  f"{bytes_downloaded}: {type(e).__name__}: {e}")
                            time.sleep(CHUNK_RETRY_DELAY)

                    if last_error is not None:
                        return False, (
                            f"Error downloading chunk at offset {bytes_downloaded} "
                            f"after {MAX_CHUNK_RETRIES} retries: {last_error}"
                        )

                    if response.status_code != 200:
                        return False, f"HTTP {response.status_code} at offset {bytes_downloaded}"

                    # Capture total file size from first response
                    if total_size == 0:
                        total_size = int(response.headers.get('X-File-Size', 0))

                    if not chunk_data:
                        break  # Empty response means offset is at/past EOF

                    # Verify chunk integrity
                    expected_sha = response.headers.get('X-Chunk-SHA256', '')
                    if expected_sha and expected_sha != 'none':
                        actual_sha = hashlib.sha256(chunk_data).hexdigest()
                        if actual_sha.lower() != expected_sha.lower():
                            return False, (
                                f"SHA-256 mismatch at offset {bytes_downloaded}: "
                                f"expected {expected_sha[:16]}... got {actual_sha[:16]}..."
                            )

                    write_buffer.append(chunk_data)
                    write_buffer_bytes += len(chunk_data)
                    bytes_downloaded += len(chunk_data)

                    is_done = ((total_size > 0 and bytes_downloaded >= total_size) or
                               len(chunk_data) < self.CHUNK_DOWNLOAD_SIZE)

                    if write_buffer_bytes >= self.WRITE_FLUSH_SIZE or is_done:
                        f.write(b''.join(write_buffer))
                        write_buffer = []
                        write_buffer_bytes = 0

                    if progress_callback:
                        progress_callback(bytes_downloaded, total_size)

                    if is_done:
                        break

            # Rename partial → final
            try:
                if os.path.exists(destination_path):
                    os.remove(destination_path)
                os.rename(partial_path, destination_path)
            except FileNotFoundError:
                # Another concurrent download of this file already renamed the partial.
                # If the final file now exists with the right size, treat as success.
                if total_size > 0 and os.path.exists(destination_path) and \
                        os.path.getsize(destination_path) == total_size:
                    return True, None
                raise
            return True, None

        except Exception as e:
            # Do NOT delete partial_path — preserve it for next resume attempt
            return False, f"Error downloading {filename}: {e}"

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
        chunk_size: int = 32768, # 32KB is the "Goldilocks" size for Pico 2 RAM
        progress_callback=None
    ) -> Tuple[bool, Optional[str], Optional[str]]:
        """
        Instrumented Stream upload for Pico 2W.
        Uses a persistent session to prevent socket exhaustion and
        32KB chunks to prevent heap fragmentation.
        """
        import time as _time
        import uuid
        import zlib
        import os

        # Initialize Telemetry Counters
        upload_start_time = _time.monotonic()
        bytes_sent = 0
        total_chunks = 0
        retries_connect = 0
        retries_read_timeout = 0
        retries_http_error = 0
        result = (False, None, "Upload not started")

        # Rolling data rate calculation (configurable window size)
        ROLLING_WINDOW_SIZE = 10  # Calculate rate over last N chunks (easy to change)
        chunk_history = []  # List of (timestamp, bytes) tuples for rolling rate

        try:
            if not os.path.exists(source_path):
                result = (False, None, f"Source file not found: {source_path}")
                return result

            total_size = os.path.getsize(source_path)
            print(f"Calculating SHA-256 of source file...")
            source_sha256 = self.calculate_file_sha256(source_path)
            session_id = str(uuid.uuid4())

            print(f"[SERVER] Starting upload: {total_size} bytes, chunk_size={chunk_size}, session={session_id[:8]}...")

            # Use Session to keep the TCP pipe open
            with requests.Session() as http_session:
                with open(source_path, 'rb') as f:
                    while bytes_sent < total_size:
                        chunk = f.read(chunk_size)
                        is_last = (bytes_sent + len(chunk) >= total_size)

                        headers = {
                            'X-Session-ID': session_id,
                            'X-Filename': destination_filename,
                            'X-Total-Size': str(total_size),
                            'X-Chunk-Size': str(len(chunk)),
                            'X-Chunk-Offset': str(bytes_sent),
                            'X-Is-Last-Chunk': 'true' if is_last else 'false',
                            'X-Chunk-CRC32': f"{zlib.crc32(chunk) & 0xFFFFFFFF:08x}",
                            'Content-Type': 'application/octet-stream'
                        }

                        chunk_success = False
                        chunk_start_time = _time.monotonic()

                        for attempt in range(15):
                            attempt_start = _time.monotonic()
                            try:
                                # (connect timeout, read timeout)
                                # 2s is enough for the handshake, 30s for the SDIO write
                                if attempt > 0:
                                    print(f"[SERVER] Chunk {total_chunks} retry {attempt} at offset {bytes_sent}")

                                response = http_session.post(
                                    f"{self.base_url}/api/upload",
                                    data=chunk,
                                    headers=headers,
                                    timeout=(2.0, 30.0)
                                )

                                attempt_duration = _time.monotonic() - attempt_start

                                # Parse response body
                                resp_body = "NO JSON"
                                try:
                                    resp_data = response.json()
                                    resp_body = f"success={resp_data.get('success', '?')}"
                                    if 'error' in resp_data:
                                        resp_body += f", error={resp_data['error']}"
                                    if 'status' in resp_data:
                                        resp_body += f", status={resp_data['status']}"
                                except:
                                    # Show full response for first few parse errors for diagnostics
                                    if total_chunks < 5:
                                        resp_body = f"PARSE_ERROR: {response.content!r}"
                                    else:
                                        resp_body = f"PARSE_ERROR (len={len(response.content)})"

                                # Detailed logging: first 10 chunks + every 20th chunk thereafter
                                if total_chunks < 10 or (total_chunks % 20 == 0):
                                    print(f"[SERVER] Chunk {total_chunks} @ offset {bytes_sent}: "
                                          f"HTTP {response.status_code}, {resp_body}")

                                # Check for device errors
                                if response.status_code == 200:
                                    try:
                                        resp_data = response.json()
                                        if not resp_data.get('success', True):
                                            print(f"[SERVER] Device returned error: {resp_data.get('error', 'Unknown error')}")
                                            result = (False, None, resp_data.get('error', 'Device Error'))
                                            return result
                                    except: pass

                                    chunk_success = True
                                    chunk_duration = _time.monotonic() - chunk_start_time

                                    if attempt > 0 or chunk_duration > 2.0:
                                        # Log if we had to retry or if chunk took a long time
                                        print(f"[SERVER] Chunk {total_chunks} completed: {len(chunk)} bytes in {chunk_duration:.2f}s ({len(chunk)/1024/chunk_duration:.1f} KB/s, {attempt} retries)")

                                    # Tiny gap to allow lwIP to process ACKs
                                    _time.sleep(0.002)
                                    break
                                else:
                                    retries_http_error += 1
                                    # Try to get response body for HTTP errors
                                    try:
                                        error_body = response.json()
                                        print(f"[SERVER] HTTP {response.status_code} at chunk {total_chunks} offset {bytes_sent}, "
                                              f"attempt {attempt+1}, duration {attempt_duration:.2f}s, body={error_body}")
                                    except:
                                        print(f"[SERVER] HTTP {response.status_code} at chunk {total_chunks} offset {bytes_sent}, "
                                              f"attempt {attempt+1}, duration {attempt_duration:.2f}s, body={response.content[:100]}")
                                    _time.sleep(0.1)

                            except requests.ConnectTimeout as e:
                                retries_connect += 1
                                attempt_duration = _time.monotonic() - attempt_start
                                print(f"[SERVER] ConnectTimeout at chunk {total_chunks} offset {bytes_sent}, "
                                      f"attempt {attempt+1}, duration {attempt_duration:.2f}s")
                                _time.sleep(0.1)
                            except requests.ReadTimeout as e:
                                retries_read_timeout += 1
                                attempt_duration = _time.monotonic() - attempt_start
                                print(f"[SERVER] ReadTimeout at chunk {total_chunks} offset {bytes_sent}, "
                                      f"attempt {attempt+1}, duration {attempt_duration:.2f}s (waited >30s)")
                                _time.sleep(0.2)
                                # If the session pipe breaks, we break the attempt loop
                                # to let the 'while' loop try to re-establish the connection.
                                break
                            except requests.ConnectionError as e:
                                retries_read_timeout += 1
                                attempt_duration = _time.monotonic() - attempt_start
                                print(f"[SERVER] ConnectionError at chunk {total_chunks} offset {bytes_sent}, "
                                      f"attempt {attempt+1}, duration {attempt_duration:.2f}s: {e}")
                                _time.sleep(0.2)
                                # If the session pipe breaks, we break the attempt loop
                                # to let the 'while' loop try to re-establish the connection.
                                break

                        if not chunk_success:
                            chunk_duration = _time.monotonic() - chunk_start_time
                            print(f"[SERVER] FAILED: Chunk stalled at offset {bytes_sent} after {chunk_duration:.1f}s and {attempt+1} attempts")
                            result = (False, None, f"Stalled at {bytes_sent}")
                            return result

                        bytes_sent += len(chunk)
                        total_chunks += 1

                        # Update rolling rate window
                        chunk_history.append((_time.monotonic(), len(chunk)))
                        if len(chunk_history) > ROLLING_WINDOW_SIZE:
                            chunk_history.pop(0)  # Keep only last N chunks

                        # Calculate rolling data rate
                        if len(chunk_history) >= 2:
                            window_duration = chunk_history[-1][0] - chunk_history[0][0]
                            window_bytes = sum(c[1] for c in chunk_history)
                            rolling_rate_kbps = (window_bytes / 1024) / window_duration if window_duration > 0 else 0
                        else:
                            rolling_rate_kbps = 0

                        if False:
                            # Progress display with rolling rate
                            progress_pct = (bytes_sent / total_size) * 100
                            print(f"\r[UPLOAD] {bytes_sent}/{total_size} bytes ({progress_pct:.1f}%) | "
                                  f"Xfer rate: {rolling_rate_kbps:.0f} KB/s (last {len(chunk_history)} chunks)", end='', flush=True)

                        if progress_callback:
                            # Pass rolling rate as third parameter for GUI display
                            # Callback signature: progress_callback(bytes_sent, total_size, rate_kbps=None)
                            try:
                                progress_callback(bytes_sent, total_size, rolling_rate_kbps)
                            except TypeError:
                                # Fallback for old callback that doesn't accept third parameter
                                progress_callback(bytes_sent, total_size)

            result = (True, source_sha256, None)
            return result

        except Exception as e:
            result = (False, None, f"Upload failed: {e}")
            return result

        finally:
            # Clear progress line
            print()  # Newline after progress display

            elapsed = _time.monotonic() - upload_start_time
            total_retries = retries_connect + retries_read_timeout + retries_http_error
            rate_kbps = (bytes_sent / 1024) / elapsed if elapsed > 0 else 0

            print(f"--- Stream Stats ---")
            print(f"  Result: {'OK' if result[0] else 'FAILED'}")
            print(f"  Sent: {bytes_sent}/{total_size} bytes ({total_chunks} chunks)")
            print(f"  Time: {elapsed:.1f}s ({rate_kbps:.1f} KB/s)")
            print(f"  Retries: {total_retries} total")
            print(f"    - ConnectTimeout (2s): {retries_connect}")
            print(f"    - ReadTimeout (30s): {retries_read_timeout}")
            print(f"    - HTTP errors: {retries_http_error}")
            if total_retries > 0:
                print(f"  Retry overhead: ~{retries_connect * 0.1 + retries_read_timeout * 0.2 + retries_http_error * 0.1:.1f}s in sleep delays")

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

    def reboot(self, timeout: int = 5) -> Tuple[bool, Optional[str]]:
        """Send a clean reboot command to the device via /api/reboot.

        The device shuts down the logger and WiFi gracefully before rebooting.
        Returns as soon as the device acknowledges the request (~200ms before reboot).

        Returns:
            Tuple of (success: bool, error_message: Optional[str])
        """
        try:
            response = requests.get(
                f"{self.base_url}/api/reboot",
                timeout=timeout
            )
            response.raise_for_status()
            data = response.json()
            if data.get('success'):
                return True, None
            else:
                return False, data.get('error', 'Unknown error')
        except Exception as e:
            return False, f"Error: {e}"

    def reflash_ep(self, uf2_filename: str, timeout: int = 120) -> Tuple[bool, Optional[str]]:
        """Trigger EP reflash on device using a UF2 file already on the device.

        The UF2 file must already be uploaded to the device's SD card root directory.
        This operation takes 10-30 seconds as it programs the EP flash via SWD.

        Args:
            uf2_filename: Name of UF2 file on device (e.g., "EP.uf2")
            timeout: HTTP timeout in seconds (default 120 for long reflash operation)

        Returns:
            Tuple of (success: bool, error_message: Optional[str])
        """
        try:
            # URL-encode the filename for the path
            import urllib.parse
            encoded_filename = urllib.parse.quote(uf2_filename, safe='')

            print(f"Triggering EP reflash with file: {uf2_filename}")
            response = requests.get(
                f"{self.base_url}/api/reflash/ep/{encoded_filename}",
                timeout=timeout
            )
            response.raise_for_status()
            data = response.json()

            if data.get('success'):
                print(f"EP reflash successful: {data.get('message', 'OK')}")
                return True, None
            else:
                error_msg = data.get('error', 'Unknown error')
                print(f"EP reflash failed: {error_msg}")
                return False, error_msg

        except requests.Timeout:
            error_msg = "Request timed out - reflash may still be in progress"
            print(f"EP reflash timeout: {error_msg}")
            return False, error_msg
        except Exception as e:
            error_msg = f"Error triggering EP reflash: {e}"
            print(error_msg)
            return False, error_msg

    def reflash_wp(self, uf2_filename: str, timeout: int = 30) -> Tuple[bool, Optional[str]]:
        """Trigger WP self-reflash using a UF2 file already on the device.

        The UF2 file must already be uploaded to the device's SD card root directory.
        This API returns immediately with an acknowledgment, then the device:
        1. Shuts down subsystems (logger, WiFi)
        2. Programs the inactive A/B partition
        3. Reboots automatically

        The device uses TBYB (Try Before You Buy) for automatic rollback
        protection if the new firmware fails to boot properly.

        Args:
            uf2_filename: Name of UF2 file on device (e.g., "WP.uf2")
            timeout: HTTP timeout in seconds (default 30 - response is immediate)

        Returns:
            Tuple of (success: bool, error_message: Optional[str])
        """
        try:
            # URL-encode the filename for the path
            import urllib.parse
            encoded_filename = urllib.parse.quote(uf2_filename, safe='')

            print(f"Triggering WP self-reflash with file: {uf2_filename}")
            print("  Device will shut down subsystems, flash, and reboot automatically.")
            response = requests.get(
                f"{self.base_url}/api/reflash/wp/{encoded_filename}",
                timeout=timeout
            )
            response.raise_for_status()
            data = response.json()

            if data.get('success'):
                message = data.get('message', 'OTA update starting')
                print(f"WP reflash initiated: {message}")
                print("  Device will reboot when complete.")
                print("  TBYB protection is active - firmware will auto-rollback if boot fails.")
                return True, None
            else:
                error_msg = data.get('error', 'Unknown error')
                print(f"WP reflash failed: {error_msg}")
                return False, error_msg

        except requests.Timeout:
            error_msg = "Request timed out before receiving acknowledgment"
            print(f"WP reflash timeout: {error_msg}")
            return False, error_msg
        except Exception as e:
            error_msg = f"Error triggering WP reflash: {e}"
            print(error_msg)
            return False, error_msg
