"""Main window for umod4 server application."""

from PySide6.QtWidgets import (QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
                               QPushButton, QLabel, QFrame, QSplitter, QTabWidget,
                               QTableWidget, QTableWidgetItem, QHeaderView, QMenu,
                               QMessageBox, QFileDialog)
from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtGui import QAction
from datetime import datetime
import os
import subprocess


class DeviceListWidget(QWidget):
    """Widget showing connected and known devices."""

    device_selected = Signal(str)  # Emits device MAC address

    def __init__(self, database):
        super().__init__()
        self.database = database
        self.selected_mac = None
        self._setup_ui()
        self.refresh_timer = QTimer()
        self.refresh_timer.timeout.connect(self.refresh_devices)
        self.refresh_timer.start(2000)  # Refresh every 2 seconds

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        # Header with Refresh button
        header_layout = QHBoxLayout()
        header = QLabel("Devices")
        header.setStyleSheet("font-weight: bold; font-size: 14px;")
        header_layout.addWidget(header)
        header_layout.addStretch()

        refresh_button = QPushButton("Refresh")
        refresh_button.setToolTip("Ping all known devices to update their status")
        refresh_button.clicked.connect(self.ping_all_devices)
        header_layout.addWidget(refresh_button)

        layout.addLayout(header_layout)

        # Device table
        self.device_table = QTableWidget()
        self.device_table.setColumnCount(8)
        self.device_table.setHorizontalHeaderLabels([
            "●", "Name", "MAC Address", "Status", "WP Version", "EP Version", "Last Seen", "Log Path"
        ])
        # Make status indicator column narrow
        self.device_table.setColumnWidth(0, 30)
        self.device_table.horizontalHeader().setStretchLastSection(True)
        self.device_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.device_table.setSelectionMode(QTableWidget.SelectionMode.SingleSelection)
        self.device_table.itemSelectionChanged.connect(self._on_selection_changed)
        self.device_table.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.device_table.customContextMenuRequested.connect(self._show_context_menu)

        layout.addWidget(self.device_table)

        self.refresh_devices()

    def refresh_devices(self):
        """Refresh the device list from database."""
        from models.database import Device
        session = self.database.get_session()
        try:
            devices = session.query(Device).all()

            self.device_table.setRowCount(len(devices))

            for row, device in enumerate(devices):
                # Status indicator column (checkmark or X)
                is_online = getattr(device, 'is_online', False)
                status_item = QTableWidgetItem("✓" if is_online else "✗")
                if is_online:
                    status_item.setForeground(Qt.GlobalColor.darkGreen)
                else:
                    status_item.setForeground(Qt.GlobalColor.red)
                status_item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
                self.device_table.setItem(row, 0, status_item)

                # Name column
                name_item = QTableWidgetItem(device.name if device.name else "")
                name_item.setData(Qt.ItemDataRole.UserRole, device.mac_address)
                self.device_table.setItem(row, 1, name_item)

                # MAC Address column
                self.device_table.setItem(row, 2, QTableWidgetItem(device.mac_address))

                # Determine status text (online if last_seen within 6 minutes = 360 seconds)
                # This matches the TimeoutMonitor threshold
                if device.last_seen:
                    seconds_ago = (datetime.utcnow() - device.last_seen).total_seconds()
                    status = "Online" if seconds_ago < 360 else f"Offline ({self._format_ago(seconds_ago)})"
                else:
                    status = "Never seen"

                # Add filesystem warning to status if there's a problem
                fs_status = getattr(device, 'filesystem_status', None)
                if fs_status and fs_status != 'ok':
                    if fs_status == 'no_card':
                        status += " ⚠️ NO SD CARD"
                    elif fs_status == 'mount_failed':
                        status += " ⚠️ FS MOUNT FAILED"

                status_item = QTableWidgetItem(status)
                # Color the status red if there's a filesystem problem
                if fs_status and fs_status != 'ok':
                    status_item.setForeground(Qt.GlobalColor.red)
                self.device_table.setItem(row, 3, status_item)
                self.device_table.setItem(row, 4, QTableWidgetItem(device.wp_version or "-"))
                self.device_table.setItem(row, 5, QTableWidgetItem(device.ep_version or "-"))

                # Convert last_seen from UTC to local time
                if device.last_seen:
                    from datetime import timezone
                    utc_time = device.last_seen.replace(tzinfo=timezone.utc)
                    local_time = utc_time.astimezone()
                    last_seen = local_time.strftime("%Y-%m-%d %H:%M:%S")
                else:
                    last_seen = "-"
                self.device_table.setItem(row, 6, QTableWidgetItem(last_seen))

                # Log path column
                self.device_table.setItem(row, 7, QTableWidgetItem(device.log_storage_path or "-"))

        finally:
            session.close()

    def _format_ago(self, seconds):
        """Format seconds into human-readable 'ago' string."""
        if seconds < 60:
            return f"{int(seconds)}s ago"
        elif seconds < 3600:
            return f"{int(seconds/60)}m ago"
        elif seconds < 86400:
            return f"{int(seconds/3600)}h ago"
        else:
            return f"{int(seconds/86400)}d ago"

    def _on_selection_changed(self):
        """Handle device selection change."""
        selected_rows = self.device_table.selectedItems()
        if selected_rows:
            row = selected_rows[0].row()
            mac_item = self.device_table.item(row, 1)  # Name column now at index 1
            if mac_item:
                self.selected_mac = mac_item.data(Qt.ItemDataRole.UserRole)
                self.device_selected.emit(self.selected_mac)

    def _show_context_menu(self, position):
        """Show context menu for device."""
        if not self.selected_mac:
            return

        menu = QMenu(self)

        rename_action = QAction("Rename Device", self)
        rename_action.triggered.connect(self._rename_device)
        menu.addAction(rename_action)

        change_path_action = QAction("Change Log Storage Path", self)
        change_path_action.triggered.connect(self._change_log_path)
        menu.addAction(change_path_action)

        menu.addSeparator()

        view_logs_action = QAction("Open Log Folder", self)
        view_logs_action.triggered.connect(self._open_log_folder)
        menu.addAction(view_logs_action)

        # Add "Manage Files on Device" option (only if device is online)
        from models.database import Device
        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=self.selected_mac).first()
            if device and device.is_online and device.last_ip:
                manage_files_action = QAction("Manage Files on Device", self)
                manage_files_action.triggered.connect(self._manage_device_files)
                menu.addAction(manage_files_action)
        finally:
            session.close()

        menu.addSeparator()

        delete_action = QAction("Delete Device", self)
        delete_action.triggered.connect(self._delete_device)
        menu.addAction(delete_action)

        menu.exec(self.device_table.viewport().mapToGlobal(position))

    def _rename_device(self):
        """Rename selected device."""
        from PySide6.QtWidgets import QInputDialog
        from models.database import Device

        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=self.selected_mac).first()
            if device:
                current_name = device.name if device.name else ""
                new_name, ok = QInputDialog.getText(
                    self, "Rename Device",
                    "Enter new name (leave blank to use MAC address):", text=current_name
                )
                if ok:
                    # Use database method to handle rename and directory move
                    success, error = self.database.update_device_name(self.selected_mac, new_name)
                    if not success:
                        QMessageBox.warning(self, "Rename Failed", f"Failed to rename device: {error}")
                    self.refresh_devices()
        finally:
            session.close()

    def _change_log_path(self):
        """Change log storage path for device."""
        from models.database import Device

        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=self.selected_mac).first()
            if not device:
                QMessageBox.warning(self, "Error", "Device not found")
                return

            # Show file dialog with option to create directories
            new_path = QFileDialog.getExistingDirectory(
                self,
                f"Select Log Storage Directory for {device.display_name}",
                device.log_storage_path,
                QFileDialog.Option.ShowDirsOnly | QFileDialog.Option.DontResolveSymlinks
            )

            if new_path:
                old_path = device.log_storage_path

                # Create directory if it doesn't exist
                try:
                    os.makedirs(new_path, exist_ok=True)
                except Exception as e:
                    QMessageBox.critical(self, "Error", f"Failed to create directory: {e}")
                    return

                # Update database
                device.log_storage_path = new_path
                session.commit()
                print(f"Changed log path for {device.display_name}: {old_path} -> {new_path}")

                QMessageBox.information(
                    self,
                    "Log Path Changed",
                    f"Log storage path updated for {device.display_name}:\n\n"
                    f"Old: {old_path}\n"
                    f"New: {new_path}\n\n"
                    f"New logs will be downloaded to the new location.\n"
                    f"Existing logs remain at the old location."
                )

                self.refresh_devices()

        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to change log path: {e}")
            print(f"Error in _change_log_path: {e}")
            import traceback
            traceback.print_exc()
        finally:
            session.close()

    def _open_log_folder(self):
        """Open log folder in file manager."""
        from models.database import Device

        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=self.selected_mac).first()
            if device and os.path.exists(device.log_storage_path):
                # Platform-specific open command
                if os.name == 'nt':  # Windows
                    os.startfile(device.log_storage_path)
                elif os.name == 'posix':  # Linux/macOS
                    subprocess.Popen(['xdg-open', device.log_storage_path])
        finally:
            session.close()

    def _manage_device_files(self):
        """Open dialog to manage files on device."""
        from models.database import Device
        from gui.device_files_dialog import DeviceFilesDialog

        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=self.selected_mac).first()
            if device and device.is_online and device.last_ip:
                dialog = DeviceFilesDialog(
                    self.database,
                    device.mac_address,
                    device.last_ip,
                    parent=self
                )
                dialog.exec()
            else:
                QMessageBox.warning(
                    self,
                    "Device Offline",
                    "Device must be online to manage files."
                )
        finally:
            session.close()

    def _delete_device(self):
        """Delete selected device from database."""
        from models.database import Device

        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=self.selected_mac).first()
            if not device:
                QMessageBox.warning(self, "Error", "Device not found")
                return

            # Confirm deletion
            reply = QMessageBox.question(
                self,
                "Confirm Deletion",
                f"Delete device '{device.display_name}' ({device.mac_address})?\n\n"
                f"This will remove:\n"
                f"- Device record from database\n"
                f"- All transfer history for this device\n"
                f"- All connection events for this device\n\n"
                f"Log files at {device.log_storage_path} will NOT be deleted.\n\n"
                f"Are you sure?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No
            )

            if reply == QMessageBox.StandardButton.Yes:
                # SQLAlchemy cascades will delete related transfers and connections
                session.delete(device)
                session.commit()
                print(f"Deleted device: {device.display_name} ({device.mac_address})")

                QMessageBox.information(
                    self,
                    "Device Deleted",
                    f"Device '{device.display_name}' has been removed from the database.\n\n"
                    f"Log files remain at: {device.log_storage_path}"
                )

                # Clear selection and refresh
                self.selected_mac = None
                self.refresh_devices()

        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to delete device: {e}")
            print(f"Error in _delete_device: {e}")
            import traceback
            traceback.print_exc()
        finally:
            session.close()

    def ping_all_devices(self):
        """Ping all known devices to check their status and update database."""
        from models.database import Device
        from device_client import DeviceClient
        from datetime import datetime

        session = self.database.get_session()
        try:
            devices = session.query(Device).all()

            for device in devices:
                # Skip devices without an IP address
                if not device.last_ip:
                    continue

                try:
                    # Create client and try to get device info
                    client = DeviceClient(device.last_ip, timeout=2)
                    info = client.get_device_info()

                    if info:
                        # Device is online - update last_seen and filesystem status
                        device.last_seen = datetime.utcnow()
                        device.is_online = True
                        device.filesystem_status = info.get('fs_status')
                        device.filesystem_message = info.get('fs_message')
                        print(f"Device {device.mac_address} is online at {device.last_ip} (fs_status: {device.filesystem_status})")
                    else:
                        # Device did not respond
                        device.is_online = False
                        device.filesystem_status = None
                        device.filesystem_message = None
                        print(f"Device {device.mac_address} did not respond at {device.last_ip}")

                except Exception as e:
                    # Device is offline or unreachable
                    device.is_online = False
                    print(f"Error pinging device {device.mac_address} at {device.last_ip}: {e}")

            session.commit()
            # Refresh UI to show updated status
            self.refresh_devices()

        except Exception as e:
            print(f"Error in ping_all_devices: {e}")
            import traceback
            traceback.print_exc()
        finally:
            session.close()


class TransferHistoryWidget(QWidget):
    """Widget showing transfer history."""

    def __init__(self, database):
        super().__init__()
        self.database = database
        self.selected_device_mac = None
        self._setup_ui()

        # Refresh timer
        self.refresh_timer = QTimer()
        self.refresh_timer.timeout.connect(self.refresh_transfers)
        self.refresh_timer.start(1000)  # Refresh every second

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        # Header
        header = QLabel("Transfer History")
        header.setStyleSheet("font-weight: bold; font-size: 14px;")
        layout.addWidget(header)

        # Transfer table
        self.transfer_table = QTableWidget()
        self.transfer_table.setColumnCount(8)
        self.transfer_table.setHorizontalHeaderLabels([
            "Name", "MAC Address", "Filename", "Size", "Progress", "Speed", "Status", "Time"
        ])
        self.transfer_table.horizontalHeader().setStretchLastSection(True)
        self.transfer_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.transfer_table.setSelectionMode(QTableWidget.SelectionMode.ExtendedSelection)
        self.transfer_table.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.transfer_table.customContextMenuRequested.connect(self._show_context_menu)

        layout.addWidget(self.transfer_table)

    def set_device_filter(self, device_mac):
        """Filter transfers by device MAC address."""
        self.selected_device_mac = device_mac
        self.refresh_transfers()

    def refresh_transfers(self):
        """Refresh transfer history from database."""
        from models.database import Transfer

        session = self.database.get_session()
        try:
            query = session.query(Transfer).order_by(Transfer.start_time.desc())

            if self.selected_device_mac:
                query = query.filter_by(device_mac=self.selected_device_mac)

            transfers = query.limit(100).all()  # Show last 100 transfers

            # Clear table first to ensure refresh
            self.transfer_table.setRowCount(0)
            self.transfer_table.setRowCount(len(transfers))

            for row, transfer in enumerate(transfers):
                # Name column
                device_name = ""
                if transfer.device and transfer.device.name:
                    device_name = transfer.device.name

                name_item = QTableWidgetItem(device_name)
                name_item.setData(Qt.ItemDataRole.UserRole, transfer.id)
                self.transfer_table.setItem(row, 0, name_item)

                # MAC Address column
                self.transfer_table.setItem(row, 1, QTableWidgetItem(transfer.device_mac))

                # Filename column
                filename_item = QTableWidgetItem(transfer.filename)
                # Store device MAC in filename column for delete operations
                filename_item.setData(Qt.ItemDataRole.UserRole, transfer.device_mac)
                self.transfer_table.setItem(row, 2, filename_item)

                # Format size
                size_mb = transfer.size_bytes / (1024 * 1024)
                self.transfer_table.setItem(row, 3, QTableWidgetItem(f"{size_mb:.2f} MB"))

                # Calculate progress for in-progress transfers
                progress_str = "-"
                if transfer.status == 'in_progress':
                    # Check actual file size on disk
                    try:
                        if transfer.device and transfer.device.log_storage_path:
                            import os
                            file_path = os.path.join(transfer.device.log_storage_path, transfer.filename)
                            if os.path.exists(file_path):
                                actual_size = os.path.getsize(file_path)
                                if transfer.size_bytes > 0:
                                    percent = (actual_size / transfer.size_bytes) * 100
                                    progress_str = f"{actual_size/(1024*1024):.2f}/{size_mb:.2f} MB ({percent:.1f}%)"
                                else:
                                    progress_str = f"{actual_size/(1024*1024):.2f} MB"
                    except:
                        pass
                elif transfer.status == 'success':
                    progress_str = "100%"
                elif transfer.status == 'failed':
                    progress_str = "Failed"

                self.transfer_table.setItem(row, 4, QTableWidgetItem(progress_str))

                # Format speed (convert MB/s to KB/s)
                if transfer.transfer_speed_mbps:
                    speed_kbps = transfer.transfer_speed_mbps * 1024
                    speed_str = f"{speed_kbps:.2f} KB/s"
                else:
                    speed_str = "-"
                self.transfer_table.setItem(row, 5, QTableWidgetItem(speed_str))

                # Status
                status_item = QTableWidgetItem(transfer.status)
                if transfer.status == 'success':
                    status_item.setForeground(Qt.GlobalColor.darkGreen)
                elif transfer.status == 'failed':
                    status_item.setForeground(Qt.GlobalColor.red)
                elif transfer.status == 'in_progress':
                    status_item.setForeground(Qt.GlobalColor.blue)
                self.transfer_table.setItem(row, 6, status_item)

                # Time (convert from UTC to local time)
                if transfer.start_time:
                    # Database stores UTC, convert to local time for display
                    from datetime import timezone
                    utc_time = transfer.start_time.replace(tzinfo=timezone.utc)
                    local_time = utc_time.astimezone()
                    time_str = local_time.strftime("%Y-%m-%d %H:%M:%S")
                else:
                    time_str = "-"
                self.transfer_table.setItem(row, 7, QTableWidgetItem(time_str))

            # Force table to update display
            self.transfer_table.viewport().update()

        except Exception as e:
            print(f"ERROR in refresh_transfers: {e}")
            import traceback
            traceback.print_exc()
        finally:
            session.close()

    def _show_context_menu(self, position):
        """Show context menu for transfer."""
        selected_rows = self.transfer_table.selectionModel().selectedRows()
        if not selected_rows:
            return

        menu = QMenu(self)

        # Single selection - allow open in viz/folder
        if len(selected_rows) == 1:
            transfer_id = self.transfer_table.item(selected_rows[0].row(), 0).data(Qt.ItemDataRole.UserRole)

            # Check if transfer is in_progress (incomplete)
            status_item = self.transfer_table.item(selected_rows[0].row(), 6)
            is_incomplete = status_item and status_item.text() == 'in_progress'

            if is_incomplete:
                retry_action = QAction("Retry/Resume Transfer", self)
                retry_action.triggered.connect(lambda: self._retry_transfer(transfer_id))
                menu.addAction(retry_action)
                menu.addSeparator()

            open_viz_action = QAction("Open in Viz Tool", self)
            open_viz_action.triggered.connect(lambda: self._open_in_viz(transfer_id))
            menu.addAction(open_viz_action)

            open_folder_action = QAction("Show in Folder", self)
            open_folder_action.triggered.connect(lambda: self._show_in_folder(transfer_id))
            menu.addAction(open_folder_action)

            menu.addSeparator()

        # Delete options (work for single or multiple selections)
        delete_local_action = QAction(f"Delete from Local Storage ({len(selected_rows)} file(s))", self)
        delete_local_action.triggered.connect(self._delete_local_files)
        menu.addAction(delete_local_action)

        delete_remote_action = QAction(f"Delete from WP LittleFS ({len(selected_rows)} file(s))", self)
        delete_remote_action.triggered.connect(self._delete_remote_files)
        menu.addAction(delete_remote_action)

        delete_both_action = QAction(f"Delete from Both Local & WP ({len(selected_rows)} file(s))", self)
        delete_both_action.triggered.connect(self._delete_both_files)
        menu.addAction(delete_both_action)

        menu.exec(self.transfer_table.viewport().mapToGlobal(position))

    def _open_in_viz(self, transfer_id):
        """Open log file in viz tool."""
        from models.database import Transfer, Device

        session = self.database.get_session()
        try:
            transfer = session.query(Transfer).get(transfer_id)
            if transfer:
                device = session.query(Device).filter_by(mac_address=transfer.device_mac).first()
                if device:
                    log_path = os.path.join(device.log_storage_path, transfer.filename)
                    if os.path.exists(log_path):
                        self._launch_viz(log_path)
                    else:
                        QMessageBox.warning(self, "File Not Found", f"Log file not found: {log_path}")
        finally:
            session.close()

    def _retry_transfer(self, transfer_id):
        """Retry/resume an incomplete transfer."""
        from models.database import Transfer, Device
        import os

        session = self.database.get_session()
        try:
            transfer = session.query(Transfer).get(transfer_id)
            if not transfer:
                QMessageBox.warning(self, "Error", "Transfer not found")
                return

            device = session.query(Device).filter_by(mac_address=transfer.device_mac).first()
            if not device:
                QMessageBox.warning(self, "Error", "Device not found")
                return

            # Delete incomplete file if it exists
            log_path = os.path.join(device.log_storage_path, transfer.filename)
            if os.path.exists(log_path):
                os.remove(log_path)
                print(f"Deleted incomplete file: {log_path}")

            # Delete the transfer record from database
            session.delete(transfer)
            session.commit()
            print(f"Deleted transfer record for {transfer.filename}")

            QMessageBox.information(
                self,
                "Transfer Reset",
                f"Transfer record deleted. The file will be re-downloaded on next device check-in.\n\n"
                f"To retry now:\n"
                f"1. Ensure WP is powered on and connected to WiFi\n"
                f"2. Wait for automatic check-in, or\n"
                f"3. Power cycle the WP device"
            )

            # Refresh display
            self.refresh_transfers()

        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to retry transfer: {e}")
            print(f"Error in _retry_transfer: {e}")
            import traceback
            traceback.print_exc()
        finally:
            session.close()

    def _show_in_folder(self, transfer_id):
        """Show log file in file manager."""
        from models.database import Transfer, Device

        session = self.database.get_session()
        try:
            transfer = session.query(Transfer).get(transfer_id)
            if transfer:
                device = session.query(Device).filter_by(mac_address=transfer.device_mac).first()
                if device:
                    log_path = os.path.join(device.log_storage_path, transfer.filename)
                    folder = os.path.dirname(log_path)
                    if os.path.exists(folder):
                        if os.name == 'nt':  # Windows
                            os.startfile(folder)
                        elif os.name == 'posix':  # Linux/macOS
                            subprocess.Popen(['xdg-open', folder])
        finally:
            session.close()

    def _delete_local_files(self):
        """Delete selected files from local storage."""
        selected_rows = self.transfer_table.selectionModel().selectedRows()
        if not selected_rows:
            return

        # Confirm deletion
        reply = QMessageBox.question(
            self, "Confirm Deletion",
            f"Are you sure you want to delete {len(selected_rows)} file(s) from local storage?\n\n"
            "This action cannot be undone.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No
        )

        if reply != QMessageBox.StandardButton.Yes:
            return

        from models.database import Transfer, Device

        session = self.database.get_session()
        try:
            deleted_count = 0
            for row_index in selected_rows:
                row = row_index.row()
                transfer_id = self.transfer_table.item(row, 0).data(Qt.ItemDataRole.UserRole)

                transfer = session.query(Transfer).get(transfer_id)
                if transfer:
                    device = session.query(Device).filter_by(mac_address=transfer.device_mac).first()
                    if device:
                        log_path = os.path.join(device.log_storage_path, transfer.filename)
                        if os.path.exists(log_path):
                            try:
                                os.remove(log_path)
                                deleted_count += 1
                            except Exception as e:
                                QMessageBox.warning(self, "Delete Failed",
                                                  f"Failed to delete {transfer.filename}: {e}")

            QMessageBox.information(self, "Deletion Complete",
                                  f"Deleted {deleted_count} file(s) from local storage.")
            self.refresh_transfers()

        finally:
            session.close()

    def _delete_remote_files(self):
        """Delete selected files from WP LittleFS."""
        selected_rows = self.transfer_table.selectionModel().selectedRows()
        if not selected_rows:
            return

        # Confirm deletion
        reply = QMessageBox.question(
            self, "Confirm Deletion",
            f"Are you sure you want to delete {len(selected_rows)} file(s) from the WP device?\n\n"
            "This action cannot be undone.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No
        )

        if reply != QMessageBox.StandardButton.Yes:
            return

        # TODO: Implement remote deletion via HTTP API
        QMessageBox.warning(self, "Not Implemented",
                          "Remote deletion from WP LittleFS is not yet implemented.\n"
                          "This will require adding an HTTP endpoint to the WP firmware.")

    def _delete_both_files(self):
        """Delete selected files from both local and remote storage."""
        selected_rows = self.transfer_table.selectionModel().selectedRows()
        if not selected_rows:
            return

        # Confirm deletion
        reply = QMessageBox.question(
            self, "Confirm Deletion",
            f"Are you sure you want to delete {len(selected_rows)} file(s) from BOTH local storage and WP device?\n\n"
            "This action cannot be undone.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No
        )

        if reply != QMessageBox.StandardButton.Yes:
            return

        # Delete local files first
        self._delete_local_files()

        # Then delete remote files
        # TODO: Implement remote deletion
        QMessageBox.information(self, "Partial Deletion",
                              "Local files deleted. Remote deletion from WP is not yet implemented.")

    def _launch_viz(self, log_path):
        """Launch viz tool with log file."""
        # Try to find viz tool
        viz_paths = [
            os.path.join(os.path.dirname(__file__), '../../logtools/viz/viz.py'),
            'viz',  # In PATH
            'viz.py'
        ]

        for viz_path in viz_paths:
            viz_full_path = os.path.abspath(viz_path) if not os.path.isabs(viz_path) else viz_path

            if os.path.exists(viz_full_path):
                # Launch viz with log file
                try:
                    subprocess.Popen(['python3', viz_full_path, log_path])
                    return
                except Exception as e:
                    QMessageBox.warning(self, "Error", f"Failed to launch viz: {e}")
                    return

        QMessageBox.warning(self, "Viz Not Found",
                          "Could not find viz tool. Please configure viz path in settings.")


class MainWindow(QMainWindow):
    """Main window for umod4 server application."""

    def __init__(self, database, server):
        super().__init__()
        self.database = database
        self.server = server
        self.setWindowTitle("umod4 Server")
        self.resize(1000, 700)
        self._setup_ui()
        self._setup_menu()

    def _setup_ui(self):
        """Set up the main UI."""
        central_widget = QWidget()
        self.setCentralWidget(central_widget)

        layout = QVBoxLayout(central_widget)

        # Server status bar
        status_frame = QFrame()
        status_frame.setFrameStyle(QFrame.Shape.Panel | QFrame.Shadow.Sunken)
        status_layout = QHBoxLayout(status_frame)

        self.server_status_label = QLabel("Server: Stopped")
        status_layout.addWidget(self.server_status_label)

        self.server_url_label = QLabel("URL: -")
        status_layout.addWidget(self.server_url_label)

        status_layout.addStretch()

        self.start_button = QPushButton("Start Server")
        self.start_button.clicked.connect(self._toggle_server)
        status_layout.addWidget(self.start_button)

        layout.addWidget(status_frame)

        # Main splitter
        splitter = QSplitter(Qt.Orientation.Vertical)

        # Top: Device list
        self.device_list = DeviceListWidget(self.database)
        self.device_list.device_selected.connect(self._on_device_selected)
        splitter.addWidget(self.device_list)

        # Bottom: Tabs for transfer history and connection log
        tab_widget = QTabWidget()

        self.transfer_history = TransferHistoryWidget(self.database)
        tab_widget.addTab(self.transfer_history, "Transfer History")

        # TODO: Add connection log widget
        # self.connection_log = ConnectionLogWidget(self.database)
        # tab_widget.addTab(self.connection_log, "Connection Log")

        splitter.addWidget(tab_widget)

        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 2)

        layout.addWidget(splitter)

    def _setup_menu(self):
        """Set up menu bar."""
        menu_bar = self.menuBar()

        # File menu
        file_menu = menu_bar.addMenu("File")

        exit_action = QAction("Exit", self)
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)

        # Settings menu
        settings_menu = menu_bar.addMenu("Settings")

        manage_devices_action = QAction("Manage Devices", self)
        manage_devices_action.triggered.connect(self._manage_devices)
        settings_menu.addAction(manage_devices_action)

        settings_menu.addSeparator()

        server_settings_action = QAction("Server Settings", self)
        server_settings_action.triggered.connect(self._show_server_settings)
        settings_menu.addAction(server_settings_action)

        # Device menu
        device_menu = menu_bar.addMenu("Device")

        delete_all_wp_action = QAction("Delete All Files on WP...", self)
        delete_all_wp_action.triggered.connect(self._delete_all_wp_files)
        device_menu.addAction(delete_all_wp_action)

        # Help menu
        help_menu = menu_bar.addMenu("Help")

        about_action = QAction("About", self)
        about_action.triggered.connect(self._show_about)
        help_menu.addAction(about_action)

    def _toggle_server(self):
        """Start or stop the server."""
        if self.server.running:
            self.server.stop()
            self.server_status_label.setText("Server: Stopped")
            self.server_url_label.setText("URL: -")
            self.start_button.setText("Start Server")
        else:
            self.server.start()
            self.server_status_label.setText(f"Server: Running on port {self.server.port}")
            self.server_url_label.setText(f"URL: {self.server.get_url()}")
            self.start_button.setText("Stop Server")

    def _on_device_selected(self, device_mac):
        """Handle device selection."""
        self.transfer_history.set_device_filter(device_mac)

    def configure_new_device(self, device_mac):
        """Show configuration dialog for newly registered device.

        Args:
            device_mac: MAC address of new device
        """
        from gui.device_config_dialog import DeviceConfigDialog
        from models.database import Device

        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=device_mac).first()
            if not device:
                return

            # Show configuration dialog
            dialog = DeviceConfigDialog(
                device_mac=device.mac_address,
                default_name=device.display_name,
                default_path=device.log_storage_path,
                parent=self
            )

            if dialog.exec() == DeviceConfigDialog.DialogCode.Accepted:
                # Update device with user's choices
                new_name, new_path = dialog.get_config()

                device.display_name = new_name
                device.log_storage_path = new_path
                session.commit()

                # Create new log directory if needed
                os.makedirs(new_path, exist_ok=True)

                # Refresh device list
                self.device_list.refresh_devices()

        finally:
            session.close()

    def _delete_all_wp_files(self):
        """Delete all files on WP device except actively writing ones."""
        from models.database import Device

        # First, check if there's a selected device
        selected_mac = None
        if hasattr(self, 'device_list') and self.device_list.selected_mac:
            selected_mac = self.device_list.selected_mac

        # If no device selected, ask user to select one
        if not selected_mac:
            session = self.database.get_session()
            try:
                devices = session.query(Device).all()
                if not devices:
                    QMessageBox.warning(self, "No Devices", "No devices found in database.")
                    return

                # Show device selection dialog
                from PySide6.QtWidgets import QInputDialog
                device_names = []
                device_macs = []
                for device in devices:
                    if device.name:
                        device_names.append(f"{device.name} ({device.mac_address})")
                    else:
                        device_names.append(device.mac_address)
                    device_macs.append(device.mac_address)

                selected_name, ok = QInputDialog.getItem(
                    self, "Select Device",
                    "Select device to delete files from:",
                    device_names, 0, False
                )

                if not ok:
                    return

                selected_mac = device_macs[device_names.index(selected_name)]
            finally:
                session.close()

        # Confirm deletion
        reply = QMessageBox.question(
            self, "Confirm Deletion",
            f"Are you sure you want to delete ALL files on the WP device {selected_mac}?\n\n"
            "This will delete all files except any that are currently being written.\n"
            "This action cannot be undone.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No
        )

        if reply != QMessageBox.StandardButton.Yes:
            return

        # TODO: Implement remote deletion via HTTP API
        QMessageBox.warning(self, "Not Implemented",
                          "Remote deletion from WP LittleFS is not yet implemented.\n"
                          "This will require adding an HTTP endpoint to the WP firmware that:\n"
                          "1. Lists all files on the LittleFS\n"
                          "2. Identifies which file is currently being written\n"
                          "3. Deletes all other files")

    def _manage_devices(self):
        """Show device management dialog."""
        from gui.manage_devices_dialog import ManageDevicesDialog

        dialog = ManageDevicesDialog(self.database, parent=self)
        dialog.exec()

        # Refresh device list after dialog closes
        self.device_list.refresh_devices()

    def _show_server_settings(self):
        """Show server settings dialog."""
        QMessageBox.information(self, "Settings", "Server settings dialog - TODO")

    def _show_about(self):
        """Show about dialog."""
        QMessageBox.about(
            self, "About umod4 Server",
            "umod4 Server\n\n"
            "Log file receiver for umod4 motorcycle data logger.\n\n"
            "Phase 0 - Development Version"
        )
