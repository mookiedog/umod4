"""Device manager for auto-downloading logs from umod4 devices.

This module handles device check-ins and automatically downloads new log files.
"""

import os
import time
import threading
from datetime import datetime
from typing import Optional, Callable
from device_client import DeviceClient
from models.database import Database, Device


class DeviceManager:
    """Manages devices and automatic log downloads."""

    def __init__(self, database: Database):
        """Initialize device manager.

        Args:
            database: Database instance
        """
        self.database = database
        self.on_download_started = None  # Callback(device_mac, filename)
        self.on_download_progress = None  # Callback(device_mac, filename, bytes_downloaded, total_bytes)
        self.on_download_completed = None  # Callback(device_mac, filename, success, error_msg)
        self._device_locks: dict = {}       # mac → Lock; prevents concurrent downloads per device
        self._device_locks_mutex = threading.Lock()
        # Live download state: (mac, filename) → {bytes, total, start_time, rate_kbps}
        self._active_downloads: dict = {}
        self._active_downloads_lock = threading.Lock()

    def get_active_downloads(self) -> dict:
        """Return a snapshot of all currently active downloads.

        Returns:
            Dict keyed by (device_mac, filename) with values:
                bytes_downloaded: int
                total_bytes: int
                start_time: float (monotonic)
                rate_kbps: float (rolling average)
        """
        with self._active_downloads_lock:
            return dict(self._active_downloads)

    def handle_device_checkin(self, device_mac: str, device_ip: str):
        """Handle device check-in notification.

        This is called when a device sends a UDP check-in packet.
        It will:
        1. Register/update device in database
        2. Connect to device and get info
        3. Download any new log files

        Args:
            device_mac: Device MAC address
            device_ip: Device IP address
        """
        print(f"DeviceManager: Handling check-in from {device_mac} at {device_ip}")

        # Get or create device in database
        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=device_mac).first()
            is_new = False

            if not device:
                # Create new device
                print(f"DeviceManager: New device detected: {device_mac}")

                if os.name == 'nt':
                    base_path = os.path.join(os.environ.get('APPDATA', ''), 'umod4_server', 'logs')
                else:
                    base_path = os.path.expanduser('~/.umod4_server/logs')

                mac_clean = device_mac.replace(':', '-')
                log_path = os.path.join(base_path, mac_clean)

                device = Device(
                    mac_address=device_mac,
                    name=None,
                    display_name=device_mac,
                    log_storage_path=log_path,
                    first_seen=datetime.utcnow(),
                    last_seen=datetime.utcnow()
                )
                session.add(device)
                is_new = True

                # Create log storage directory
                os.makedirs(log_path, exist_ok=True)
            else:
                # Update last seen time, IP, and online status
                device.last_seen = datetime.utcnow()

            # Update IP and online status for both new and existing devices
            device.last_ip = device_ip
            device.is_online = True

            session.commit()

            # Get device info before closing session
            device_mac = device.mac_address
            log_storage_path = device.log_storage_path
            device_display_name = device.display_name

        finally:
            session.close()

        # Record connection event
        self.database.add_connection(device_mac, device_ip)

        # Create client and download logs
        client = DeviceClient(device_ip)

        # Get device info
        info = client.get_device_info()
        if info:
            print(f"DeviceManager: Device info: {info}")

            # Update device version info
            session = self.database.get_session()
            try:
                device = session.query(Device).filter_by(mac_address=device_mac).first()
                if device:
                    if 'wp_version' in info:
                        # wp_version is a JSON object like {"GH":"...", "BT":"..."}
                        # Convert to string for database storage
                        import json
                        wp_ver = info['wp_version']
                        if isinstance(wp_ver, dict):
                            device.wp_version = json.dumps(wp_ver)
                        else:
                            device.wp_version = wp_ver
                    if 'ep_version' in info:
                        device.ep_version = info.get('ep_version')
                    session.commit()
            finally:
                session.close()

        # Download new log files
        self._download_new_logs(device_mac, log_storage_path, device_display_name, client)

    def _download_new_logs(self, device_mac: str, log_storage_path: str, device_name: str, client: DeviceClient):
        """Download new log files from device.

        Args:
            device_mac: Device MAC address
            log_storage_path: Local directory to save logs
            client: DeviceClient instance
        """
        # Prevent concurrent downloads for the same device (e.g. two rapid check-ins)
        with self._device_locks_mutex:
            if device_mac not in self._device_locks:
                self._device_locks[device_mac] = threading.Lock()
            lock = self._device_locks[device_mac]

        if not lock.acquire(blocking=False):
            print(f"DeviceManager: Download already in progress for {device_mac}, skipping check-in")
            return

        RETRY_DELAY = 30   # seconds between retries after a failure
        MAX_RETRIES = 10   # give up after this many consecutive failures

        try:
            for attempt in range(MAX_RETRIES):
                had_failure = self._download_new_logs_locked(device_mac, log_storage_path, device_name, client)
                if not had_failure:
                    break
                print(f"DeviceManager: Download had failures for {device_mac}, "
                      f"retrying in {RETRY_DELAY}s (attempt {attempt + 1}/{MAX_RETRIES})")
                time.sleep(RETRY_DELAY)
        finally:
            lock.release()

    def _find_previously_downloaded(self, device_mac: str, log_storage_path: str, filename: str, file_size: int):
        """Return the Transfer record of a previously downloaded copy of this file, or None.

        Queries the database for a successful transfer matching device_mac, filename,
        and size_bytes, then verifies the file still exists on disk.
        """
        transfer = self.database.find_successful_transfer(device_mac, filename, file_size)
        if transfer is None:
            return None
        date_str = transfer.start_time.strftime("%Y-%m-%d")
        candidate = os.path.join(log_storage_path, date_str, filename)
        if os.path.exists(candidate):
            return transfer
        return None

    def _download_new_logs_locked(self, device_mac: str, log_storage_path: str, device_name: str, client: DeviceClient) -> bool:
        """Inner download loop, called only when the per-device lock is held.

        Returns True if any download failed (so the caller can retry).
        """
        had_failure = False

        # Get list of files on device
        files = client.list_log_files()
        if not files:
            print(f"DeviceManager: No log files found on device {device_mac}")
            return False

        print(f"DeviceManager: Device has {len(files)} log files")

        # Create date subdirectory for today's downloads
        date_str = datetime.now().strftime("%Y-%m-%d")
        date_subdir = os.path.join(log_storage_path, date_str)
        os.makedirs(date_subdir, exist_ok=True)

        # Check which files are new (not already downloaded)
        for file_info in files:
            filename = file_info['filename']
            file_size = file_info['size']

            # Only sync log files (skip uploaded files like .uf2, etc.)
            if not (filename.endswith('.um4') or filename.endswith('.log')):
                continue

            # Skip files with no size — likely the currently-open active log
            if file_size == 0:
                print(f"DeviceManager: Skipping {filename} (size 0, probably active log)")
                continue

            local_path = os.path.join(date_subdir, filename)

            # Check if file already exists
            if os.path.exists(local_path):
                # File exists - check if size matches
                local_size = os.path.getsize(local_path)
                if local_size == file_size:
                    print(f"DeviceManager: Skipping {filename} (already downloaded)")
                    continue
                else:
                    print(f"DeviceManager: Re-downloading {filename} (size mismatch: {local_size} vs {file_size})")

            # Check if this file was already downloaded on a previous date.
            # If so, record it as deduplicated and skip the download.
            prior_transfer = self._find_previously_downloaded(device_mac, log_storage_path, filename, file_size)
            if prior_transfer is not None:
                prior_date = prior_transfer.start_time.strftime("%Y-%m-%d")
                note = f"Already downloaded on {prior_date}"
                dedup_transfer = self.database.add_transfer(
                    device_mac=device_mac,
                    filename=filename,
                    size_bytes=file_size,
                    status='deduplicated',
                )
                self.database.update_transfer(
                    dedup_transfer.id,
                    end_time=datetime.utcnow(),
                    error_message=note,
                )
                print(f"DeviceManager: Skipping {filename} ({note})")
                continue

            # Download file
            print(f"DeviceManager: Downloading {filename} ({file_size} bytes)")

            # Notify callback
            if self.on_download_started:
                self.on_download_started(device_mac, filename)

            # Reuse an existing in_progress record if one exists (retry case),
            # otherwise create a fresh one.
            from models.database import Transfer
            existing_session = self.database.get_session()
            try:
                existing = existing_session.query(Transfer).filter_by(
                    device_mac=device_mac, filename=filename, status='in_progress'
                ).first()
                if existing:
                    transfer_id = existing.id
                else:
                    transfer_id = None
            finally:
                existing_session.close()

            if transfer_id is not None:
                transfer = type('_T', (), {'id': transfer_id})()  # lightweight holder
            else:
                transfer = self.database.add_transfer(
                    device_mac=device_mac,
                    filename=filename,
                    size_bytes=file_size,
                    status='in_progress'
                )

            start_time = datetime.utcnow()
            mono_start = time.monotonic()

            # Register active download for live progress/speed tracking
            dl_key = (device_mac, filename)
            with self._active_downloads_lock:
                self._active_downloads[dl_key] = {
                    'bytes_downloaded': 0,
                    'total_bytes': file_size,
                    'start_time': mono_start,
                    'rate_kbps': 0.0,
                }

            # Define progress callback — updates live state and forwards to external callback
            # Rate is calculated only over bytes transferred in this session (not resume offset).
            _session_start_bytes = [None]  # set on first call

            def progress_callback(bytes_downloaded, total_bytes, _fn=filename, _mac=device_mac, _key=dl_key):
                if _session_start_bytes[0] is None:
                    _session_start_bytes[0] = bytes_downloaded
                elapsed = time.monotonic() - mono_start
                session_bytes = bytes_downloaded - _session_start_bytes[0]
                rate_kbps = (session_bytes / 1024) / elapsed if elapsed > 0 and session_bytes > 0 else 0.0
                with self._active_downloads_lock:
                    if _key in self._active_downloads:
                        self._active_downloads[_key]['bytes_downloaded'] = bytes_downloaded
                        self._active_downloads[_key]['total_bytes'] = total_bytes
                        self._active_downloads[_key]['rate_kbps'] = rate_kbps
                if self.on_download_progress:
                    self.on_download_progress(_mac, _fn, bytes_downloaded, total_bytes)

            # Download file
            success, error_msg = client.download_log_file(
                filename,
                local_path,
                progress_callback=progress_callback
            )

            # Remove from active downloads
            with self._active_downloads_lock:
                self._active_downloads.pop(dl_key, None)

            # Calculate transfer speed
            end_time = datetime.utcnow()
            duration = (end_time - start_time).total_seconds()
            if duration > 0:
                speed_mbps = (file_size / (1024 * 1024)) / duration
            else:
                speed_mbps = 0

            # Verify SHA-256 if download succeeded
            sha256_hash = None
            if success:
                # Get SHA-256 from device
                device_sha256 = client.get_file_sha256(filename)

                if device_sha256:
                    # Calculate local SHA-256
                    local_sha256 = client.calculate_file_sha256(local_path)

                    if device_sha256.lower() == local_sha256.lower():
                        print(f"DeviceManager: SHA-256 verified for {filename}")
                        sha256_hash = device_sha256.lower()
                    else:
                        print(f"DeviceManager: SHA-256 MISMATCH for {filename}!")
                        print(f"  Device: {device_sha256}")
                        print(f"  Local:  {local_sha256}")
                        success = False
                        error_msg = f"SHA-256 verification failed (device: {device_sha256[:16]}..., local: {local_sha256[:16]}...)"
                        # Delete corrupted file
                        os.remove(local_path)
                else:
                    print(f"DeviceManager: WARNING: Could not get SHA-256 from device for {filename}")

            # Update transfer record
            if success:
                self.database.update_transfer(
                    transfer.id,
                    status='success',
                    end_time=end_time,
                    transfer_speed_mbps=speed_mbps,
                    sha256=sha256_hash
                )
                print(f"DeviceManager: Downloaded {filename} successfully ({speed_mbps:.2f} MB/s) -> {local_path}")
            else:
                had_failure = True
                self.database.update_transfer(
                    transfer.id,
                    status='failed',
                    end_time=end_time,
                    error_message=error_msg
                )
                print(f"DeviceManager: Failed to download {filename}: {error_msg}")

            # Notify callback
            if self.on_download_completed:
                self.on_download_completed(device_mac, filename, success, error_msg)

        return had_failure
