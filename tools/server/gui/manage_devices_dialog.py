"""Device management dialog."""

from PySide6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QPushButton,
                               QTableWidget, QTableWidgetItem, QHeaderView,
                               QMessageBox, QInputDialog, QFileDialog, QLabel)
from PySide6.QtCore import Qt
import os


class ManageDevicesDialog(QDialog):
    """Dialog for managing all known devices."""

    def __init__(self, database, parent=None):
        super().__init__(parent)
        self.database = database
        self.setWindowTitle("Manage Devices")
        self.setModal(True)
        self.resize(700, 400)
        self._setup_ui()
        self.refresh_devices()

    def _setup_ui(self):
        """Set up the dialog UI."""
        layout = QVBoxLayout(self)

        # Header
        header = QLabel("All Known Devices")
        header.setStyleSheet("font-weight: bold; font-size: 14px;")
        layout.addWidget(header)

        # Device table
        self.device_table = QTableWidget()
        self.device_table.setColumnCount(4)
        self.device_table.setHorizontalHeaderLabels([
            "Display Name", "MAC Address", "WP Version", "Log Storage Path"
        ])
        self.device_table.horizontalHeader().setStretchLastSection(True)
        self.device_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.device_table.setSelectionMode(QTableWidget.SelectionMode.SingleSelection)
        layout.addWidget(self.device_table)

        # Buttons
        button_layout = QHBoxLayout()

        self.rename_button = QPushButton("Rename Device")
        self.rename_button.clicked.connect(self._rename_device)
        button_layout.addWidget(self.rename_button)

        self.change_path_button = QPushButton("Change Log Path")
        self.change_path_button.clicked.connect(self._change_log_path)
        button_layout.addWidget(self.change_path_button)

        self.delete_button = QPushButton("Delete Device")
        self.delete_button.clicked.connect(self._delete_device)
        button_layout.addWidget(self.delete_button)

        button_layout.addStretch()

        self.manage_files_button = QPushButton("Manage Files on Device")
        self.manage_files_button.clicked.connect(self._manage_files)
        button_layout.addWidget(self.manage_files_button)

        self.upload_button = QPushButton("Upload File to Device")
        self.upload_button.clicked.connect(self._upload_file)
        button_layout.addWidget(self.upload_button)

        # Reflash buttons
        self.reflash_ep_button = QPushButton("Reflash EP")
        self.reflash_ep_button.clicked.connect(self._reflash_ep)
        self.reflash_ep_button.setToolTip("Upload a UF2 file and reflash the EP processor via SWD")
        button_layout.addWidget(self.reflash_ep_button)

        self.reflash_wp_button = QPushButton("Reflash WP")
        self.reflash_wp_button.setEnabled(False)
        self.reflash_wp_button.setToolTip("WP reflash not yet implemented")
        button_layout.addWidget(self.reflash_wp_button)

        self.close_button = QPushButton("Close")
        self.close_button.clicked.connect(self.accept)
        button_layout.addWidget(self.close_button)

        layout.addLayout(button_layout)

    def refresh_devices(self):
        """Refresh the device list from database."""
        from models.database import Device

        session = self.database.get_session()
        try:
            devices = session.query(Device).order_by(Device.display_name).all()

            self.device_table.setRowCount(len(devices))

            for row, device in enumerate(devices):
                # Display name
                name_item = QTableWidgetItem(device.display_name)
                name_item.setData(Qt.ItemDataRole.UserRole, device.mac_address)
                self.device_table.setItem(row, 0, name_item)

                # MAC address
                self.device_table.setItem(row, 1, QTableWidgetItem(device.mac_address))

                # WP version
                wp_version = device.wp_version or "-"
                self.device_table.setItem(row, 2, QTableWidgetItem(wp_version))

                # Log storage path
                self.device_table.setItem(row, 3, QTableWidgetItem(device.log_storage_path))

        finally:
            session.close()

    def _get_selected_mac(self):
        """Get MAC address of selected device.

        Returns:
            str or None: MAC address if device selected, None otherwise
        """
        selected_rows = self.device_table.selectedItems()
        if not selected_rows:
            QMessageBox.warning(self, "No Selection", "Please select a device first.")
            return None

        row = selected_rows[0].row()
        mac_item = self.device_table.item(row, 0)
        return mac_item.data(Qt.ItemDataRole.UserRole)

    def _rename_device(self):
        """Rename selected device."""
        from models.database import Device

        mac_address = self._get_selected_mac()
        if not mac_address:
            return

        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=mac_address).first()
            if not device:
                return

            new_name, ok = QInputDialog.getText(
                self,
                "Rename Device",
                f"Enter new name for {mac_address}:",
                text=device.display_name
            )

            if ok and new_name.strip():
                device.display_name = new_name.strip()
                session.commit()
                self.refresh_devices()

        finally:
            session.close()

    def _change_log_path(self):
        """Change log storage path for selected device."""
        from models.database import Device

        mac_address = self._get_selected_mac()
        if not mac_address:
            return

        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=mac_address).first()
            if not device:
                return

            new_path = QFileDialog.getExistingDirectory(
                self,
                "Select Log Storage Directory",
                device.log_storage_path
            )

            if new_path:
                device.log_storage_path = new_path
                session.commit()
                os.makedirs(new_path, exist_ok=True)
                self.refresh_devices()

        finally:
            session.close()

    def _delete_device(self):
        """Delete selected device from database."""
        from models.database import Device

        mac_address = self._get_selected_mac()
        if not mac_address:
            return

        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=mac_address).first()
            if not device:
                return

            # Confirm deletion
            reply = QMessageBox.question(
                self,
                "Delete Device",
                f"Delete device '{device.display_name}' ({mac_address})?\n\n"
                f"The device will be re-discovered and auto-configured with defaults "
                f"the next time it connects.\n\n"
                f"Log files in {device.log_storage_path} will NOT be deleted.",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No
            )

            if reply == QMessageBox.StandardButton.Yes:
                session.delete(device)
                session.commit()
                self.refresh_devices()

                QMessageBox.information(
                    self,
                    "Device Deleted",
                    f"Device {mac_address} has been deleted.\n\n"
                    f"It will be re-discovered with default settings when it next connects."
                )

        finally:
            session.close()

    def _manage_files(self):
        """Open file management dialog for selected device."""
        from models.database import Device
        from gui.device_files_dialog import DeviceFilesDialog

        mac_address = self._get_selected_mac()
        if not mac_address:
            return

        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=mac_address).first()
            if not device:
                return

            # Check if device is online
            if not device.is_online or not device.last_ip:
                QMessageBox.warning(
                    self,
                    "Device Offline",
                    f"Device '{device.display_name}' is not currently online.\n\n"
                    f"Make sure the device is powered on and connected to the network."
                )
                return

            # Open the file management dialog
            dialog = DeviceFilesDialog(
                self.database,
                mac_address,
                device.last_ip,
                self
            )
            dialog.exec()

        finally:
            session.close()

    def _upload_file(self):
        """Upload a file to the selected device."""
        from models.database import Device, DeviceUpload
        from device_client import DeviceClient
        from PySide6.QtWidgets import QProgressDialog
        from PySide6.QtCore import QThread, Signal
        from datetime import datetime

        mac_address = self._get_selected_mac()
        if not mac_address:
            return

        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=mac_address).first()
            if not device:
                return

            # Check if device is online
            if not device.is_online or not device.last_ip:
                QMessageBox.warning(
                    self,
                    "Device Offline",
                    f"Device '{device.display_name}' is not currently online.\n\n"
                    f"Make sure the device is powered on and connected to the network."
                )
                return

            # Let user select file to upload
            source_file, _ = QFileDialog.getOpenFileName(
                self,
                "Select File to Upload",
                "",
                "All Files (*.*)"
            )

            if not source_file:
                return  # User cancelled

            # Extract filename for destination
            import os
            destination_filename = os.path.basename(source_file)

            # Confirm upload
            file_size = os.path.getsize(source_file)
            reply = QMessageBox.question(
                self,
                "Confirm Upload",
                f"Upload file to device '{device.display_name}'?\n\n"
                f"Source: {source_file}\n"
                f"Destination: /{destination_filename}\n"
                f"Size: {file_size:,} bytes\n\n"
                f"The file will be uploaded in chunks with SHA-256 verification.",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No
            )

            if reply != QMessageBox.StandardButton.Yes:
                return

            # Create progress dialog
            progress = QProgressDialog(
                f"Uploading {destination_filename}...",
                "Cancel",
                0,
                100,
                self
            )
            progress.setWindowTitle("File Upload")
            progress.setModal(True)
            progress.setMinimumDuration(0)
            progress.setValue(0)

            # Upload in a separate thread to keep UI responsive
            # For simplicity, we'll do it synchronously here but show progress

            client = DeviceClient(device.last_ip)

            def update_progress(bytes_sent, total_bytes):
                percent = int((bytes_sent / total_bytes) * 100)
                progress.setValue(percent)
                progress.setLabelText(
                    f"Uploading {destination_filename}...\n"
                    f"{bytes_sent:,} / {total_bytes:,} bytes ({percent}%)"
                )
                # Process events to keep UI responsive
                from PySide6.QtWidgets import QApplication
                QApplication.processEvents()

                # Check if user cancelled
                if progress.wasCanceled():
                    raise Exception("Upload cancelled by user")

            # Create database record for this upload
            upload_record = DeviceUpload(
                device_mac=mac_address,
                source_path=source_file,
                destination_filename=destination_filename,
                size_bytes=file_size,
                start_time=datetime.utcnow(),
                status='in_progress'
            )
            session.add(upload_record)
            session.commit()
            upload_id = upload_record.id

            try:
                start_time = datetime.utcnow()

                success, sha256, error_msg = client.upload_file(
                    source_file,
                    destination_filename,
                    progress_callback=update_progress
                )

                end_time = datetime.utcnow()
                duration = (end_time - start_time).total_seconds()
                transfer_speed_mbps = (file_size * 8 / 1_000_000) / duration if duration > 0 else 0

                progress.close()

                # Update database record
                upload_record = session.query(DeviceUpload).get(upload_id)
                if upload_record:
                    upload_record.end_time = end_time
                    upload_record.transfer_speed_mbps = transfer_speed_mbps
                    upload_record.sha256 = sha256
                    upload_record.status = 'success' if success else 'failed'
                    upload_record.error_message = error_msg
                    session.commit()

                if success:
                    QMessageBox.information(
                        self,
                        "Upload Complete",
                        f"File uploaded successfully!\n\n"
                        f"File: {destination_filename}\n"
                        f"Location: /{destination_filename}\n"
                        f"SHA-256: {sha256[:16]}...{sha256[-16:]}\n"
                        f"Speed: {transfer_speed_mbps:.2f} Mbps\n\n"
                        f"The file is now available on the device."
                    )
                else:
                    QMessageBox.critical(
                        self,
                        "Upload Failed",
                        f"Failed to upload file:\n\n{error_msg}"
                    )

            except Exception as e:
                progress.close()

                # Update database record with error
                upload_record = session.query(DeviceUpload).get(upload_id)
                if upload_record:
                    upload_record.end_time = datetime.utcnow()
                    upload_record.status = 'failed'
                    upload_record.error_message = str(e)
                    session.commit()

                if "cancelled" not in str(e).lower():
                    QMessageBox.critical(
                        self,
                        "Upload Error",
                        f"An error occurred during upload:\n\n{str(e)}"
                    )

        finally:
            session.close()

    def _reflash_ep(self):
        """Reflash the EP processor on the selected device."""
        from models.database import Device
        from device_client import DeviceClient
        from PySide6.QtWidgets import QProgressDialog, QApplication

        mac_address = self._get_selected_mac()
        if not mac_address:
            return

        session = self.database.get_session()
        try:
            device = session.query(Device).filter_by(mac_address=mac_address).first()
            if not device:
                return

            # Check if device is online
            if not device.is_online or not device.last_ip:
                QMessageBox.warning(
                    self,
                    "Device Offline",
                    f"Device '{device.display_name}' is not currently online.\n\n"
                    f"Make sure the device is powered on and connected to the network."
                )
                return

            # Let user select UF2 file to upload
            source_file, _ = QFileDialog.getOpenFileName(
                self,
                "Select EP Firmware File",
                "",
                "UF2 Files (*.uf2);;All Files (*.*)"
            )

            if not source_file:
                return  # User cancelled

            # Extract filename
            destination_filename = os.path.basename(source_file)

            # Warn if filename is not EP.uf2
            if destination_filename.lower() != "ep.uf2":
                reply = QMessageBox.warning(
                    self,
                    "Filename Warning",
                    f"The selected file is named '{destination_filename}'.\n\n"
                    f"Typically EP firmware files are named 'EP.uf2'.\n\n"
                    f"Are you sure you want to continue with this file?",
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                    QMessageBox.StandardButton.No
                )
                if reply != QMessageBox.StandardButton.Yes:
                    return

            # Final confirmation
            file_size = os.path.getsize(source_file)
            reply = QMessageBox.warning(
                self,
                "Confirm EP Reflash",
                f"This will reflash the EP processor on device '{device.display_name}'.\n\n"
                f"File: {source_file}\n"
                f"Size: {file_size:,} bytes\n\n"
                f"WARNING: Do not power off the device during reflash!\n\n"
                f"The device will be temporarily unresponsive during the process "
                f"(approximately 10-30 seconds).\n\n"
                f"Continue with EP reflash?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No
            )

            if reply != QMessageBox.StandardButton.Yes:
                return

            client = DeviceClient(device.last_ip)

            # Create progress dialog for upload phase
            progress = QProgressDialog(
                f"Uploading {destination_filename}...",
                "Cancel",
                0, 100, self
            )
            progress.setWindowTitle("EP Reflash - Upload")
            progress.setModal(True)
            progress.setMinimumDuration(0)
            progress.setValue(0)

            upload_cancelled = False

            def update_upload_progress(bytes_sent, total_bytes):
                nonlocal upload_cancelled
                percent = int((bytes_sent / total_bytes) * 100)
                progress.setValue(percent)
                progress.setLabelText(
                    f"Uploading {destination_filename}...\n"
                    f"{bytes_sent:,} / {total_bytes:,} bytes ({percent}%)"
                )
                QApplication.processEvents()

                if progress.wasCanceled():
                    upload_cancelled = True
                    raise Exception("Upload cancelled by user")

            try:
                # Upload the UF2 file
                success, sha256, error_msg = client.upload_file(
                    source_file,
                    destination_filename,
                    progress_callback=update_upload_progress
                )

                progress.close()

                if upload_cancelled:
                    return

                if not success:
                    QMessageBox.critical(
                        self,
                        "Upload Failed",
                        f"Failed to upload firmware file:\n\n{error_msg}"
                    )
                    return

                # Now trigger the reflash
                progress = QProgressDialog(
                    "Reflashing EP processor...\n\n"
                    "This may take 10-30 seconds.\n"
                    "Do not power off the device!",
                    None,  # No cancel button during reflash
                    0, 0,  # Indeterminate progress
                    self
                )
                progress.setWindowTitle("EP Reflash - Programming")
                progress.setModal(True)
                progress.setMinimumDuration(0)

                QApplication.processEvents()

                # Trigger the reflash with extended timeout
                success, error_msg = client.reflash_ep(destination_filename, timeout=120)

                progress.close()

                if success:
                    QMessageBox.information(
                        self,
                        "EP Reflash Complete",
                        f"EP processor reflashed successfully!\n\n"
                        f"The device should now be running the new firmware."
                    )
                else:
                    QMessageBox.critical(
                        self,
                        "EP Reflash Failed",
                        f"Failed to reflash EP processor:\n\n{error_msg}"
                    )

            except Exception as e:
                progress.close()
                if "cancelled" not in str(e).lower():
                    QMessageBox.critical(
                        self,
                        "Reflash Error",
                        f"An error occurred during EP reflash:\n\n{str(e)}"
                    )

        finally:
            session.close()
